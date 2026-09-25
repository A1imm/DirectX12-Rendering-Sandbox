#include "System.h"
#include <windowsx.h>

bool System::Initialize(unsigned int width, unsigned int height, bool fullscreen)
{
	if (!CreateMainWindows(width, height, fullscreen))
	{
		return false;
	}

	m_renderWidget = std::make_unique<RenderWidget>(width, height, m_hwnd);
	
	return true;
}

void System::Shutdown()
{
	ShutdownWindows();
}

void System::Run()
{
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			m_renderWidget->Draw();
		}

	}
}

LRESULT CALLBACK System::MessageHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}

		case WM_CLOSE:
		{
			PostQuitMessage(0);
			return 0;
		}

		case WM_KEYDOWN:
		{
			//Close on ESC
			if (wparam == VK_ESCAPE)
			{
				PostQuitMessage(0);
				return 0;
			}
			break;
		}

		case WM_SIZE:
			if (wparam != SIZE_MINIMIZED)
			{
				const unsigned int width = LOWORD(lparam);
				const unsigned int height = HIWORD(lparam);
				OnResize(width, height);
			}
			return 0;

		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			OnMouseDown(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		}
		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
		{
			OnMouseUp(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			OnMouseMove(wparam, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			return 0;
		}
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

bool System::CreateMainWindows(unsigned int screenWidth, unsigned int screenHeight, bool fullscreen)
{
	WNDCLASS wc;
	int posX, posY;

	ApplicationHandle = this;

	m_hinstance = GetModuleHandle(NULL);

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = m_hinstance;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = 0;
	wc.lpszClassName = m_applicationName;

	const ATOM classId = RegisterClass(&wc);
	if (classId == 0)
	{
		assert(false && "Can't register the window class");
		return false;
	}

	DWORD windowStyle = 0;
	if (fullscreen)
	{
		DEVMODE dmScreenSettings;
		memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
		dmScreenSettings.dmSize = sizeof(dmScreenSettings);
		dmScreenSettings.dmPelsWidth = (unsigned long)screenWidth;
		dmScreenSettings.dmPelsHeight = (unsigned long)screenHeight;
		dmScreenSettings.dmBitsPerPel = 32;
		dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
		ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN);
		m_fullScreen = true;
		posX = posY = 0;

		SetWindowLong(m_hwnd, GWL_STYLE, ~(WS_CAPTION | WS_THICKFRAME));
		SetWindowLong(m_hwnd, GWL_EXSTYLE, ~(WS_EX_DLGMODALFRAME |WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
		windowStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP;
	}
	else
	{
		RECT R = { 0, 0, static_cast<LONG>(screenWidth), static_cast<LONG>(screenHeight) };
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
		posX = (GetSystemMetrics(SM_CXSCREEN) - screenWidth) / 2;
		posY = (GetSystemMetrics(SM_CYSCREEN) - screenHeight) / 2;
		windowStyle = WS_OVERLAPPEDWINDOW;
	}

	m_hwnd = CreateWindow(m_applicationName, m_applicationName, windowStyle, posX, posY, screenWidth, screenHeight, NULL, NULL, m_hinstance, NULL);
	if (m_hwnd == 0)
	{
		assert(false && "Can't create the window");
		return false;
	}

	ShowWindow(m_hwnd, SW_SHOW);
	SetFocus(m_hwnd);
	ShowCursor(true);
	return true;
}

void System::ShutdownWindows()
{
	ShowCursor(true);

	if (m_fullScreen)
	{
		ChangeDisplaySettings(NULL, 0);
	}

	DestroyWindow(m_hwnd);
	m_hwnd = NULL;

	UnregisterClass(m_applicationName, m_hinstance);
	m_hinstance = NULL;

	ApplicationHandle = nullptr;
}

void System::OnMouseDown(WPARAM btnState, int x, int y)
{
	m_lastMousePos.x = x;
	m_lastMousePos.y = y;
	m_renderWidget->UpdateWorldViewProjectionBuffer();
	SetCapture(m_hwnd);
}
void System::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void System::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - m_lastMousePos.x));
		float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - m_lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		m_renderWidget->GetCamera().Theta += dx;
		m_renderWidget->GetCamera().Phi += dy;

		// Restrict the angle mPhi.
		//mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f * static_cast<float>(x - m_lastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - m_lastMousePos.y);

		// Update the camera radius based on input.
		m_renderWidget->GetCamera().Radius += dx - dy;
	}

	m_renderWidget->UpdateWorldViewProjectionBuffer();
	m_lastMousePos.x = x;
	m_lastMousePos.y = y;
}

void System::OnResize(unsigned int width, unsigned int height)
{
	if (m_renderWidget)
	{
		m_renderWidget->Resize(width, height);
		m_renderWidget->UpdateWorldViewProjectionBuffer();
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam)
{
	return ApplicationHandle->MessageHandler(hwnd, umessage, wparam, lparam);
}
