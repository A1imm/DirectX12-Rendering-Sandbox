#include "System.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pScmdline, int iCmdshow)
{
	System system;
	if (system.Initialize(640, 480, false))
	{
		system.Run();
	}

	system.Shutdown();
	return 0;
}
