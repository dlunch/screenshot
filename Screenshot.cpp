#include <WinSock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>

//http://msdn.microsoft.com/en-us/library/windows/desktop/ms533843%28v=vs.85%29.aspx
int GetEncoderCLSID(const WCHAR* format, CLSID* pClsid)
{
	UINT  num = 0;          // number of image encoders
	UINT  size = 0;         // size of the image encoder array in bytes

	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

	Gdiplus::GetImageEncodersSize(&num, &size);
	if(size == 0)
		return -1;  // Failure

	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if(pImageCodecInfo == NULL)
		return -1;  // Failure

	Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

	for(UINT j = 0; j < num; ++j)
	{
		if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 )
		{
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;  // Success
		}    
	}

	free(pImageCodecInfo);
	return -1;  // Failure
}

inline void SendString(SOCKET s, std::string string)
{
	send(s, string.c_str(), string.size(), 0);
}

inline void AppendVector(std::vector<uint8_t> &vector, std::string &string)
{
	vector.insert(vector.end(), string.begin(), string.end());
}

template<typename T>
inline std::string ToString(T data)
{
	std::stringstream str;
	str << data;
	return str.str();
}

void ProcessHotKey()
{
	//Capture
	size_t width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	size_t height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	size_t left = GetSystemMetrics(SM_XVIRTUALSCREEN);
	size_t top = GetSystemMetrics(SM_YVIRTUALSCREEN);

	HDC screenDC = GetDC(NULL);
	HDC memDC = CreateCompatibleDC(screenDC);
	HBITMAP bmp = CreateCompatibleBitmap(screenDC, width, height);
	SelectObject(memDC, bmp);
	BitBlt(memDC, 0, 0, width, height, screenDC, left, top, SRCCOPY);

	CLSID pngClsid;
	GetEncoderCLSID(L"image/png", &pngClsid);
	Gdiplus::Bitmap bitmap(bmp, NULL);

	IStream *memStream;
	CreateStreamOnHGlobal(NULL, TRUE, &memStream);
	bitmap.Save(memStream, &pngClsid);
	STATSTG stat;
	memStream->Stat(&stat, 0);

	DeleteObject(bmp);
	DeleteDC(memDC);

	SYSTEMTIME st;
	FILETIME ft;
	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	uint64_t epoch = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL
	epoch = epoch / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
	
	std::string filename = "scrshot-" + ToString(epoch) + ".png";
	std::string hostname = "dlunch.net";
	std::string uri = "/scrshot.php";
	std::string boundary = "---------------Boundary151235fa";

	//Prepare body
	std::vector<uint8_t> body;
	AppendVector(body, "--" + boundary + "\r\n");
	AppendVector(body, "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n");
	AppendVector(body, std::string("Content-Type: application/octet-stream\r\n\r\n"));

	size_t oldSize = body.size();
	body.resize(oldSize + (size_t)stat.cbSize.QuadPart);
	
	LARGE_INTEGER zero;
	zero.QuadPart = 0;
	memStream->Seek(zero, STREAM_SEEK_SET, NULL);
	memStream->Read(&*(body.begin() + oldSize), (size_t)stat.cbSize.QuadPart, NULL);
	memStream->Release();

	AppendVector(body, "\r\n--" + boundary + "--\r\n\r\n");

	//Upload
	addrinfo *info;
	getaddrinfo(hostname.c_str(), "http", NULL, &info);

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	connect(s, info->ai_addr, info->ai_addrlen);

	SendString(s, "POST " + uri + " HTTP/1.1\r\n");
	SendString(s, "Host:" + hostname + "\r\n");
	SendString(s, "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
	SendString(s, "Content-Length: " + ToString(body.size()) + "\r\n\r\n");
	send(s, (const char *)&*body.begin(), body.size(), 0);

	std::string data;
	while(true)
	{
		char buf[256];
		int n = recv(s, buf, 255, 0);
		if(n <= 0)
			break;
		buf[n] = 0;
		data.append(buf);
	}

	int begin, end = 0;
	while(true)
	{
		begin = data.find("\r\n", end + 1) + 2;
		end = data.find("\r\n", begin);
		if(begin == std::string::npos || end == std::string::npos)
			break;
		std::string line = data.substr(begin, end - begin + 1);
		if(line.substr(0, 7) == "http://")
		{
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, line.size());
			memcpy(GlobalLock(hMem), line.c_str(), line.size());
			GlobalUnlock(hMem);
			OpenClipboard(0);
			EmptyClipboard();
			SetClipboardData(CF_TEXT, hMem);
			CloseClipboard();
		}
	}

	MessageBox(NULL, TEXT("Done"), TEXT("Done"), MB_OK);
	closesocket(s);
}

int CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	WSADATA ws;
	WSAStartup(MAKEWORD(2, 2), &ws);

	CoInitialize(NULL);

	//We need to create windows to receive hotkey notifications

	WNDCLASSEX wcex;
	ZeroMemory(&wcex, sizeof(wcex));
	wcex.cbSize = sizeof(wcex);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszClassName = TEXT("Screenshot");
	wcex.lpfnWndProc = (WNDPROC)WndProc;

	HWND hWnd = CreateWindowEx(NULL, (LPCWSTR)RegisterClassEx(&wcex), TEXT("Screenshot"), WS_OVERLAPPED, -1, -1, -1, -1, NULL, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_HIDE);

	MSG msg;
	while(GetMessage(&msg, 0, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	CoUninitialize();
	WSACleanup();
	Gdiplus::GdiplusShutdown(gdiplusToken);

	return msg.wParam;
}

int CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	switch(iMessage)
	{
	case WM_CREATE:
		RegisterHotKey(hWnd, 0, NULL, VK_SNAPSHOT);
		break;
	case WM_HOTKEY:
		ProcessHotKey();
		break;
	}
	return DefWindowProc(hWnd, iMessage, wParam, lParam);
}