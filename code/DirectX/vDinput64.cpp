// vDinput64.cpp
// Claude: x64 only (D-057). The Windows SDK has no x64 dinput.lib, only dinput8.lib, which lacks the DirectInput 5
// entry point the joystick force feedback uses. Windows still ships it in dinput.dll, so load it from there.

#ifdef _WIN64

#include <windows.h>
#include "vdinput.h"

typedef HRESULT (WINAPI *direct_input_create_fn)(HINSTANCE, DWORD, LPDIRECTINPUTA *, LPUNKNOWN);

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD version, LPDIRECTINPUTA *di, LPUNKNOWN outer)
{
	static direct_input_create_fn create = NULL;

	if (create == NULL) {
		HMODULE dll = LoadLibrary("dinput.dll");
		if (dll != NULL) {
			create = (direct_input_create_fn)GetProcAddress(dll, "DirectInputCreateA");
		}
		if (create == NULL) {
			return E_FAIL;
		}
	}
	return create(hinst, version, di, outer);
}

#endif
