#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winioctl.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#pragma comment(lib, "comctl32.lib")

HWND listView = nullptr, logBox = nullptr;

void Log(const std::wstring& message) {
    if (!logBox) return;
    int n = GetWindowTextLengthW(logBox);
    SendMessageW(logBox, EM_SETSEL, n, n);
    std::wstring line = message + L"\r\n";
    SendMessageW(logBox, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
}

std::wstring AtaString(const std::vector<BYTE>& data, int firstWord, int wordCount) {
    std::wstring value;
    for (int i = 0; i < wordCount; ++i) {
        const int p = (firstWord + i) * 2;
        if (p + 1 >= (int)data.size()) break;
        value += (wchar_t)data[p + 1];
        value += (wchar_t)data[p];
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == 0))
        value.pop_back();
    while (!value.empty() && value.front() == L' ')
        value.erase(value.begin());
    return value;
}

unsigned short AtaWord(const std::vector<BYTE>& data, int word) {
    const int p = word * 2;
    if (p + 1 >= (int)data.size()) return 0;
    return (unsigned short)(data[p] | (data[p + 1] << 8));
}

std::wstring DescriptorString(const BYTE* buffer, DWORD bytesReturned, DWORD offset) {
    if (!offset || offset >= bytesReturned) return L"";
    const char* p = reinterpret_cast<const char*>(buffer + offset);
    const char* end = reinterpret_cast<const char*>(buffer + bytesReturned);
    std::wstring result;
    while (p < end && *p) {
        result += (wchar_t)(unsigned char)*p++;
    }
    return result;
}

void RefreshDrives() {
    if (!listView) return;
    ListView_DeleteAllItems(listView);

    for (DWORD index = 0; index < 32; ++index) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
        HANDLE drive = CreateFileW(path.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE) continue;

        BYTE buffer[2048]{};
        DWORD bytesReturned = 0;
        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;

        if (DeviceIoControl(drive, IOCTL_STORAGE_QUERY_PROPERTY,
                &query, sizeof(query), buffer, sizeof(buffer), &bytesReturned, nullptr)
            && bytesReturned >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
            GET_LENGTH_INFORMATION lengthInfo{};
            ULONGLONG capacityBytes = 0;
            DWORD lengthReturned = 0;
            if (DeviceIoControl(drive, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                    &lengthInfo, sizeof(lengthInfo), &lengthReturned, nullptr)) {
                capacityBytes = (ULONGLONG)lengthInfo.Length.QuadPart;
            }

            std::wstringstream capacity;
            capacity << std::fixed << std::setprecision(1)
                     << (double)capacityBytes / 1073741824.0 << L" GiB";

            const std::wstring number = std::to_wstring(index);
            const std::wstring model = DescriptorString(buffer, bytesReturned, descriptor->ProductIdOffset);
            const std::wstring label = (model.empty() ? L"Unknown model" : model) + L" | " + capacity.str();
            const std::wstring serial = DescriptorString(buffer, bytesReturned, descriptor->SerialNumberOffset);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = ListView_GetItemCount(listView);
            item.pszText = const_cast<LPWSTR>(number.c_str());
            const int row = ListView_InsertItem(listView, &item);
            ListView_SetItemText(listView, row, 1, const_cast<LPWSTR>(label.c_str()));
            ListView_SetItemText(listView, row, 2, const_cast<LPWSTR>(serial.c_str()));
        }
        CloseHandle(drive);
    }
    Log(L"Drive list refreshed using Windows storage-property queries.");
}

void IdentifySelectedDrive() {
    const int row = ListView_GetNextItem(listView, -1, LVNI_SELECTED);
    if (row < 0) {
        MessageBoxW(nullptr, L"Select a physical drive first.", L"Begate ATA Explorer", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t numberText[32]{};
    ListView_GetItemText(listView, row, 0, numberText, 32);
    const DWORD index = (DWORD)_wtoi(numberText);
    const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);

    HANDLE drive = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (drive == INVALID_HANDLE_VALUE) {
        Log(L"Could not open selected drive. Win32 error: " + std::to_wstring(GetLastError()));
        return;
    }

    struct AtaPacket {
        ATA_PASS_THROUGH_EX passThrough;
        ULONG alignmentFiller;
        BYTE data[512];
    } packet{};

    packet.passThrough.Length = sizeof(ATA_PASS_THROUGH_EX);
    packet.passThrough.AtaFlags = ATA_FLAGS_DATA_IN;
    packet.passThrough.DataTransferLength = sizeof(packet.data);
    packet.passThrough.TimeOutValue = 10;
    packet.passThrough.DataBufferOffset = offsetof(AtaPacket, data);
    packet.passThrough.CurrentTaskFile[6] = 0xEC; // IDENTIFY DEVICE (read-only)

    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(drive, IOCTL_ATA_PASS_THROUGH,
        &packet, sizeof(packet), &packet, sizeof(packet), &bytesReturned, nullptr);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(drive);

    if (!ok) {
        Log(L"ATA pass-through was rejected. Win32 error: " + std::to_wstring(error)
            + L". The controller, bridge, driver, or permissions may not support this operation.");
        return;
    }

    const std::vector<BYTE> data(packet.data, packet.data + sizeof(packet.data));
    const ULONGLONG sectors =
        (ULONGLONG)AtaWord(data, 100) |
        ((ULONGLONG)AtaWord(data, 101) << 16) |
        ((ULONGLONG)AtaWord(data, 102) << 32) |
        ((ULONGLONG)AtaWord(data, 103) << 48);

    std::wstringstream report;
    report << L"READ-ONLY IDENTIFY DEVICE RESULT\r\n"
           << L"PhysicalDrive: " << index << L"\r\n"
           << L"Model: " << AtaString(data, 27, 20) << L"\r\n"
           << L"Firmware: " << AtaString(data, 23, 4) << L"\r\n"
           << L"Serial: " << AtaString(data, 10, 10) << L"\r\n"
           << L"48-bit sector count: " << sectors << L"\r\n"
           << L"No write, erase, or firmware-write command was issued.";
    Log(report.str());
    MessageBoxW(nullptr, report.str().c_str(), L"ATA Identify Results", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateWindowW(L"BUTTON", L"Refresh Drives", WS_CHILD | WS_VISIBLE,
            12, 12, 130, 30, window, (HMENU)1, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Identify Selected (Read-only)", WS_CHILD | WS_VISIBLE,
            152, 12, 230, 30, window, (HMENU)2, nullptr, nullptr);

        listView = CreateWindowW(WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            12, 52, 850, 260, window, (HMENU)3, GetModuleHandleW(nullptr), nullptr);
        ListView_SetExtendedListViewStyle(listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<LPWSTR>(L"PhysicalDrive #"); column.cx = 120;
        ListView_InsertColumn(listView, 0, &column);
        column.pszText = const_cast<LPWSTR>(L"Model / Capacity"); column.cx = 430;
        ListView_InsertColumn(listView, 1, &column);
        column.pszText = const_cast<LPWSTR>(L"Serial"); column.cx = 250;
        ListView_InsertColumn(listView, 2, &column);

        logBox = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            12, 322, 850, 200, window, (HMENU)4, nullptr, nullptr);
        Log(L"Begate Hardware Control Lab — ATA Explorer prototype.");
        RefreshDrives();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) RefreshDrives();
        if (LOWORD(wParam) == 2) IdentifySelectedDrive();
        return 0;
    case WM_SIZE: {
        const int width = LOWORD(lParam), height = HIWORD(lParam);
        if (listView) MoveWindow(listView, 12, 52, std::max(400, width - 24), std::max(100, height - 280), TRUE);
        if (logBox) MoveWindow(logBox, 12, std::max(160, height - 220), std::max(400, width - 24), 195, TRUE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"BegateATAExplorer";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&windowClass)) return 1;

    HWND window = CreateWindowW(windowClass.lpszClassName, L"Begate Hardware Control Lab — ATA Explorer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, showCommand);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}