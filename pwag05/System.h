#ifndef _SYSTEMCLASS_H_
#define _SYSTEMCLASS_H_

#include "Renderwidget.h"

class System
{
public:
	System() = default;
	//System class is non-copyable:
	System(const System&)=delete;
	System(System&&) = delete;
	System& operator=(const System&) = delete;
	System& operator=(System&&) = delete;

	bool Initialize(unsigned int width, unsigned int height, bool fullscreen);
	void Shutdown();
	void Run();

	LRESULT CALLBACK MessageHandler(HWND, UINT, WPARAM, LPARAM);

private:
	bool CreateMainWindows(unsigned int width, unsigned int height, bool fullscreen);
	void ShutdownWindows();

	//Events
	void OnMouseDown(WPARAM btnState, int x, int y);
	void OnMouseUp(WPARAM btnState, int x, int y);
	void OnMouseMove(WPARAM btnState, int x, int y);
	void OnResize(unsigned int width, unsigned int height);
	POINT m_lastMousePos{0,0};

private:
	const LPCWSTR m_applicationName = L"PWAG";
	HINSTANCE m_hinstance =0;
	HWND m_hwnd = 0;
	std::unique_ptr<RenderWidget> m_renderWidget;
	bool m_fullScreen = false;
};


static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static System* ApplicationHandle = 0;

#endif