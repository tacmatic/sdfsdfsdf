#include "GUI.h"
#include "../Utilities/User.h"
#include "../Utilities/skCrypter.h"
#include "../Utilities/VKNames.h"
#include "../Utilities/Colors.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"
#include "../ImGui/imgui_internal.h"
#include <windows.h>
#include <thread>
#include <io.h>
#include <fcntl.h>
#include <iostream>


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void BindCrtHandlesToStdHandles(bool bindStdIn, bool bindStdOut, bool bindStdErr);

static int ConvertToHex(float color[3])
{
	int A = 0xFF;
	int R = (int)(color[0] * 255.0f);
	int G = (int)(color[1] * 255.0f);
	int B = (int)(color[2] * 255.0f);

	// Move bytes to the left and merge them
	return (A << 24) | (R << 16) | (G << 8) | B;
}

static void ConvertToRGB(int hex, float color[3])
{
	// Move bytes to the right and extract them
	color[0] = ((hex >> (16)) & 0x000000ff) / 255.0f;
	color[1] = ((hex >> (8)) & 0x000000ff) / 255.0f;
	color[2] = ((hex >> (0)) & 0x000000ff) / 255.0f;
};

Menu::Menu(bool& running, User& user)
{
	m_Running = &running;
	m_User = &user;

	// Konvertiere Farbwerte
	ConvertToRGB(m_User->SearchSettings.color, m_User->SearchSettings.colorRGB);

	// Starte den Menü-Rendering-Thread
	std::thread threadMenu(&Menu::MenuThread, this);
	threadMenu.detach();  // Thread im Hintergrund laufen lassen
}

std::string get_random_process_name()
{
	const char* names[] = { "Calculator", "Notepad", "Explorer", "Cmd" };
	int index = rand() % 4;
	return names[index];
}


void Menu::Start() {

	// Erstelle Konsole, wenn im User.ini-File aktiviert
	if (m_User->Data.debug) {
		AllocConsole();
		BindCrtHandlesToStdHandles(true, true, true);
	}

	// Fensterklasse erstellen
	WNDCLASSEXW wc = {
		sizeof(wc),               // Größe der Struktur
		CS_CLASSDC,               // Fensterklasse
		WndProc,                  // Fensterprozedur
		0L,                       // Weitere Parameter
		0L,                       // Weitere Parameter
		GetModuleHandle(nullptr), // Handle des Moduls
		nullptr,                  // Icon
		nullptr,                  // Cursor
		nullptr,                  // Hintergrund
		nullptr,                  // Menü
		L"Xbox",                  // Fenstername
		nullptr                   // Symbol
	};

	::RegisterClassExW(&wc);

	// Fenstergröße definieren
	int windowWidth = 320;
	int windowHeight = 480;

	// Fenster erstellen
	HWND hwnd = ::CreateWindowW(
		wc.lpszClassName,
		_(L"Xbox"),
		WS_POPUP,
		0,
		0,
		windowWidth, windowHeight,
		nullptr, nullptr,
		wc.hInstance, nullptr
	);

	// Stelle sicher, dass hwnd und wc initialisiert sind, bevor sie verwendet werden
	if (hwnd == nullptr) {
		std::cerr << "[Error] Fenster konnte nicht erstellt werden." << std::endl;
		return;
	}

	// Direkt3D initialisieren
	if (!CreateDeviceD3D(hwnd)) {
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		std::cerr << "[ImGui] Konnte Direct3D nicht initialisieren" << std::endl;
		return;
	}

	// Fenster anzeigen
	bool hidden = false;
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// ImGui Kontext erstellen
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL;

	// ImGui-Stil setzen
	StyleMenu();

	// Plattform/Renderer Backends setzen
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

	// Haupt-Rendering-Schleife
	while (*m_Running) {
		// Nachrichten abfragen und verarbeiten (Eingaben, Fenstergröße, etc.)
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				*m_Running = false;
		}
		if (!*m_Running)
			break;

		if (GetAsyncKeyState(m_User->Binds.toggleMenu) & 0x01) hidden = !hidden;
		if (hidden) {
			::ShowWindow(hwnd, SW_HIDE);
			Sleep(10);
			continue;
		}
		else {
			::ShowWindow(hwnd, SW_SHOWDEFAULT);
		}

		// Starten des ImGui-Frames
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Vollbildansicht setzen
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);

		// Rendern
		if (!m_User->Data.loadedApp) {
			DrawLoading();
		}
		else {
			DrawMenu();
		}

		// Rendering durchführen
		ImGui::Render();
		m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Präsentation mit vsync
		m_pSwapChain->Present(1, 0);
	}

	// Aufräumen
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

void Menu::MenuThread()
{
	// Setze den Konsolentitel, um den Prozess zu tarnen (wichtig für den Vanguard-Bypass)
	std::string process_name = get_random_process_name(); // Funktion zur Generierung eines zufälligen Prozesses
	SetConsoleTitleW((LPCWSTR)process_name.c_str());

	// Erstelle das Fenster und initialisiere das Rendering
	::CreateWindowW(wc.lpszClassName, _(L"Xbox"), WS_POPUP, 0, 0, 320, 480, nullptr, nullptr, wc.hInstance, nullptr);
	CreateDeviceD3D(hwnd); // Erstelle D3D-Geräte
	CreateRenderTarget(); // Erstelle Renderziel
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);

	// Haupt-Rendering-Schleife
	while (*m_Running)
	{
		// Rendern des Menüs
		DrawMenu(); // Hier verwenden wir Ihre Funktion zum Rendern des Menüs.

		// Rendering-Pause, um Systemressourcen zu schonen
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Ressourcen bereinigen, wenn der Thread beendet wird
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

void Menu::DrawLoading()
{
	ImGui::Begin(_(""), m_Running,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImGui::Dummy(ImVec2(0.0f, 96.0f));
	ImGui::Dummy(ImVec2(112.0f, 0.0f)); ImGui::SameLine();
	ImU32 redSeny = ImGui::ColorConvertFloat4ToU32(Color::redSeny);
	Spinner("##LOADING", 32, 5, redSeny);

	ImGui::Dummy(ImVec2(0.0f, 20.0f));
	ImGui::Dummy(ImVec2(112.0f, 0.0f)); ImGui::SameLine();
	ImGui::Text(_("Loading"));

	ImGui::End();
}

const int MONITOR_CENTER_X = GetSystemMetrics(SM_CXSCREEN) / 2;
const int MONITOR_CENTER_Y = GetSystemMetrics(SM_CYSCREEN) / 2;

//https://github.com/soufianekhiat/DearWidgets
bool Slider2D(char const* pLabel, int pFov, int pSize, void* p_valueX, void* p_valueY, int p_minX, int p_maxX, int p_minY, int p_maxY)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiID const iID = ImGui::GetID(pLabel);
	ImGui::PushID(iID);
	
	// Render frame
	ImVec2 const size(pFov, pFov);
	ImVec2 vPos = ImGui::GetCursorScreenPos();
	ImRect oRect(vPos, ImVec2(vPos.x + size.x, vPos.y + size.y));
	ImU32 const uFrameCol = ImGui::GetColorU32(ImGuiCol_FrameBg);
	ImVec2 const vOriginPos = ImGui::GetCursorScreenPos();
	ImGui::RenderFrame(oRect.Min, oRect.Max, uFrameCol, true, 0.0f);
	ImGui::Dummy(size);
	
	// Mouse state
	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(oRect, ImGui::GetID("##Zone"), &hovered, &held, ImGuiButtonFlags_AllowOverlap);

	// Update position
	ImU64 s_delta_x = static_cast<ImU64>(static_cast<ImU16>(p_maxX) - static_cast<ImU16>(p_minX));
	ImU64 s_delta_y = static_cast<ImU64>(static_cast<ImU16>(p_maxY) - static_cast<ImU16>(p_minY));
	bool bModified = false;

	if (hovered && held)
	{
		ImVec2 const vCursorPos = ImVec2(ImGui::GetMousePos().x - oRect.Min.x, ImGui::GetMousePos().y - oRect.Min.y);
		
		float fValueX = vCursorPos.x / (oRect.Max.x - oRect.Min.x) * static_cast<float>(*reinterpret_cast<ImU16*>(&s_delta_x)) + static_cast<float>(static_cast<ImU16>((ImU64)p_minX));
		float fValueY = vCursorPos.y / (oRect.Max.y - oRect.Min.y) * static_cast<float>(*reinterpret_cast<ImU16*>(&s_delta_y)) + static_cast<float>(static_cast<ImU16>((ImU64)p_minY));

		ImU64 s_value_x = static_cast<ImU64>(static_cast<ImU16>(fValueX));
		ImU64 s_value_y = static_cast<ImU64>(static_cast<ImU16>(fValueY));

		*reinterpret_cast<ImU16*>((ImU64*)p_valueX) = *reinterpret_cast<ImU16*>(&s_value_x);
		*reinterpret_cast<ImU16*>((ImU64*)p_valueY) = *reinterpret_cast<ImU16*>(&s_value_y);

		bModified = true;
	}

	// Update values
	ImU64 s_clamped_x = static_cast<ImU64>(ImClamp(*reinterpret_cast<ImU16*>(p_valueX), static_cast<ImU16>(p_minX), static_cast<ImU16>(p_maxX)));
	ImU64 s_clamped_y = static_cast<ImU64>(ImClamp(*reinterpret_cast<ImU16*>(p_valueY), static_cast<ImU16>(p_minY), static_cast<ImU16>(p_maxY)));
	*reinterpret_cast<ImU16*>((ImU64*)p_valueX) = *reinterpret_cast<ImU16*>(&s_clamped_x);
	*reinterpret_cast<ImU16*>((ImU64*)p_valueY) = *reinterpret_cast<ImU16*>(&s_clamped_y);

	float const fScaleX = (static_cast<float>(*reinterpret_cast<ImU16*>((ImU64*)p_valueX)) - static_cast<float>(static_cast<ImU16>((ImU64)p_minX))) / static_cast<float>(*reinterpret_cast<ImU16*>(&s_delta_x));
	float const fScaleY = (static_cast<float>(*reinterpret_cast<ImU16*>((ImU64*)p_valueY)) - static_cast<float>(static_cast<ImU16>((ImU64)p_minY))) / static_cast<float>(*reinterpret_cast<ImU16*>(&s_delta_y));

	ImVec2 const vCursorPos((oRect.Max.x - oRect.Min.x) * fScaleX + oRect.Min.x, (oRect.Max.y - oRect.Min.y) * fScaleY + oRect.Min.y);

	// Draw size search 
	ImDrawList* pDrawList = ImGui::GetWindowDrawList();

	pSize = pSize < 5 ? 5 : pSize; // Check for minimum size

	// Check for boundaries
	pDrawList->AddRectFilled(
		ImVec2((vCursorPos.x - pSize / 2) < oRect.Min.x ? oRect.Min.x : (vCursorPos.x - pSize / 2), // Top
			(vCursorPos.y - pSize / 2) < oRect.Min.y ? oRect.Min.y : (vCursorPos.y - pSize / 2)), // Left
		ImVec2((vCursorPos.x + pSize / 2) > oRect.Max.x ? oRect.Max.x : (vCursorPos.x + pSize / 2), // Bottom
			(vCursorPos.y + pSize / 2) > oRect.Max.y ? oRect.Max.y : (vCursorPos.y + pSize / 2)), // Right
		ImGui::ColorConvertFloat4ToU32(Color::redSeny));

	ImGui::PopID();
	
	return bModified;
}

void Menu::DrawMenu() {
	
	ImGui::Begin(_("Discord: tutryx"), m_Running,
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImGui::Text(_("Strength")); ImGui::SameLine(); Menu::HelpMark(_("Amount of movement produced."));
	Menu::SliderPercentage(_("##STR"), &m_User->Aimbot.strength, 1.0f, 100.0f);

	ImGui::Text(_("FOV")); ImGui::SameLine(); Menu::HelpMark(_("Square of detection in pixels."));
	if (Menu::SliderInteger(_("##FOV"), &m_User->Aimbot.fov, 1, 320))
	{
		//PreferenceXY has to be inside FOV
		m_User->SearchSettings.refX = m_User->SearchSettings.refX > m_User->Aimbot.fov 
			? m_User->Aimbot.fov : m_User->SearchSettings.refX;
		m_User->SearchSettings.refY = m_User->SearchSettings.refY > m_User->Aimbot.fov
			? m_User->Aimbot.fov : m_User->SearchSettings.refY;
		m_User->SearchSettings.prefX = MONITOR_CENTER_X - m_User->Aimbot.fov / 2 + m_User->SearchSettings.refX;
		m_User->SearchSettings.prefY = MONITOR_CENTER_Y - m_User->Aimbot.fov / 2 + m_User->SearchSettings.refY;
		
		// Size search can't be bigger than the FOV
		m_User->SearchSettings.sizeSearch = m_User->SearchSettings.sizeSearch > m_User->Aimbot.fov
			? m_User->Aimbot.fov : m_User->SearchSettings.sizeSearch;
	}

	ImGui::Text(_("Offset X")); ImGui::SameLine(); Menu::HelpMark(_("Offset X from left to right."));
	Menu::SliderInteger(_("##OFX"), &m_User->Aimbot.offsetX, 0, m_User->Aimbot.fov);

	ImGui::Text(_("Offset Y")); ImGui::SameLine(); Menu::HelpMark(_("Offset Y from top to bottom."));
	Menu::SliderInteger(_("##OFY"), &m_User->Aimbot.offsetY, 0, m_User->Aimbot.fov);


	ImGui::Separator();


	// This has been done in a hurry so the implementation can be better
	ImGui::Text(_("Start position of the search:"));
	if (Slider2D("##REF", m_User->Aimbot.fov, m_User->SearchSettings.sizeSearch, &m_User->SearchSettings.refX, &m_User->SearchSettings.refY, 0, m_User->Aimbot.fov, 0, m_User->Aimbot.fov))
	{
		// Update PreferenceXY
		m_User->SearchSettings.prefX = MONITOR_CENTER_X - m_User->Aimbot.fov / 2 + m_User->SearchSettings.refX;
		m_User->SearchSettings.prefY = MONITOR_CENTER_Y - m_User->Aimbot.fov / 2 + m_User->SearchSettings.refY;
	}

	ImGui::Text(_("Size Search")); ImGui::SameLine(); Menu::HelpMark(_("Size of the sub-search square."));
	if (Menu::SliderInteger(_("##SSN"), &m_User->SearchSettings.sizeSearch, 1, m_User->Aimbot.fov))
		// Minimum Match can't be higher than the number of pixels in the sub-search square
		m_User->SearchSettings.minMatch = m_User->SearchSettings.minMatch > pow(m_User->SearchSettings.sizeSearch, 2) 
		? pow(m_User->SearchSettings.sizeSearch, 2) : m_User->SearchSettings.minMatch;

	ImGui::Text(_("Minimum Match")); ImGui::SameLine(); Menu::HelpMark(_("Minimum number of pixels to match.\n[CTRL + CLICK] for precise input"));
	Menu::SliderInteger(_("##MMN"), &m_User->SearchSettings.minMatch, 1, pow(m_User->SearchSettings.sizeSearch, 2));

	ImGui::Text(_("Color variaton")); ImGui::SameLine(); Menu::HelpMark(_("Variation of the color, the more,\nthe less of a perfect match it needs\n(0 for perfect match)"));
	Menu::SliderInteger(_("##CVA"), &m_User->SearchSettings.colorVariation, 0, 64);

	if (ImGui::ColorPicker3(_("##COLOR"), m_User->SearchSettings.colorRGB,
		ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoAlpha | 
		ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoOptions))
		m_User->SearchSettings.color = ConvertToHex(m_User->SearchSettings.colorRGB);


	ImGui::Separator();


	ImGui::Text(_("Aim:")); ImGui::SameLine(120.0f);
	Menu::HotKey(_("##HK1"), m_User->Binds.aim, ImVec2(150, 0));

	ImGui::Text(_("Hide menu: ")); ImGui::SameLine(120.0f);
	Menu::HotKey(_("##HK2"), m_User->Binds.toggleMenu, ImVec2(150, 0));

	ImGui::End();
}

bool Menu::CreateDeviceD3D(HWND hWnd) {
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void Menu::CleanupDeviceD3D() {
	CleanupRenderTarget();
	if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
	if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
	if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void Menu::CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
	pBackBuffer->Release();
}

void Menu::CleanupRenderTarget()
{
	if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}

bool Menu::SliderPercentage(const char* label, float* v, float v_min, float v_max)
{
	bool interacted = false;
	float f = *v * 100.0f;
	if (ImGui::SliderFloat(label, &f, v_min, v_max, "", ImGuiSliderFlags_AlwaysClamp))
	{
		*v = f / 100.0f;
		interacted = true;
	}
	ImGui::SameLine(); ImGui::Text(_("%.0f%%"), f);
	return interacted;
}

bool Menu::SliderInteger(const char* label, int* v, int v_min, int v_max)
{
	bool interacted = false;
	if(ImGui::SliderInt(label, v, v_min, v_max, "", ImGuiSliderFlags_AlwaysClamp)) interacted = true;
	ImGui::SameLine(); ImGui::Text(_("%d"), *v);
	return interacted;
}

// https://github.com/xvorost/ImGui-Custom-HotKeys
void Menu::HotKey(const char* hkId, int& hotkey, const ImVec2& size)
{
	const ImGuiID id = ImGui::GetID(hkId);
	ImGui::PushID(id);
	if (ImGui::GetActiveID() == id) {
		ImGui::SetActiveID(id, ImGui::GetCurrentWindow());

		// Draw active button
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
		ImGui::Button("...", size);
		ImGui::PopStyleColor();

		// Get key
		for (int key = 0x01; key < 0xFF; key++)
		{
			if (!(GetAsyncKeyState(key) & 0x8000)) continue;
			if (key == VK_BACK) key = 0x00; // Unbind with Back space
			hotkey = key;
			ImGui::ClearActiveID();
			return;
		}
	}
	// Draw and update button state
	else if (ImGui::Button(VKNames.at(hotkey), size)) {
		ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
		return;
	}

	ImGui::PopID();
	return;
}

void Menu::HelpMark(const char* desc) {
	ImGui::TextDisabled("(?)");
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

// https://github.com/ocornut/imgui/issues/1901#issue-335266223
bool Menu::Spinner(const char* label, float radius, int thickness, const ImU32& color) {
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	// Render
	window->DrawList->PathClear();

	int num_segments = 30;
	int start = abs(ImSin(g.Time * 1.8f) * (num_segments - 5));

	const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
	const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

	const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

	for (int i = 0; i < num_segments; i++)
	{
		const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
		window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + g.Time * 8) * radius,
			centre.y + ImSin(a + g.Time * 8) * radius));
	}

	window->DrawList->PathStroke(color, false, thickness);
}

void Menu::StyleMenu()
{
	ImGuiStyle* style = &ImGui::GetStyle();
	ImVec4* colors = style->Colors;

	style->WindowPadding = ImVec2(15, 15);
	style->WindowRounding = 5.0f;
	style->FramePadding = ImVec2(5, 5);
	style->FrameRounding = 4.0f;
	style->ItemSpacing = ImVec2(12, 8);
	style->ItemInnerSpacing = ImVec2(8, 6);
	style->IndentSpacing = 25.0f;
	style->ScrollbarSize = 15.0f;
	style->ScrollbarRounding = 9.0f;
	style->GrabMinSize = 5.0f;
	style->GrabRounding = 3.0f;

	style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.83f, 0.88f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
	style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);

	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	case WM_NCHITTEST:
	{ // Handle WS_POPUP movement and resize
		constexpr int TITLEBAR = 20;
		constexpr int BUTTONS = 32;
		constexpr int BORDER = 4;

		RECT clientRect;
		GetClientRect(hWnd, &clientRect);
		POINT clientPoint{ LOWORD(lParam), HIWORD(lParam) };
		ScreenToClient(hWnd, &clientPoint);

		// Handle resize by the borders
		/*if (clientPoint.y <= BORDER && clientPoint.x <= BORDER) return HTTOPLEFT;
		if (clientPoint.y <= BORDER && clientPoint.x >= (clientRect.right - BORDER)) return HTTOPRIGHT;
		if (clientPoint.y >= (clientRect.bottom - BORDER) && clientPoint.x <= BORDER) return HTBOTTOMLEFT;
		if (clientPoint.y >= (clientRect.bottom - BORDER) && clientPoint.x >= (clientRect.right - BORDER)) return HTBOTTOMRIGHT;
		if (clientPoint.y <= BORDER) return HTTOP;
		if (clientPoint.x <= BORDER) return HTLEFT;
		if (clientPoint.y >= (clientRect.bottom - BORDER)) return HTBOTTOM;
		if (clientPoint.x >= (clientRect.right - BORDER)) return HTRIGHT;*/

		// Handle movement by titlebar
		if (clientPoint.y <= TITLEBAR && clientPoint.x < (clientRect.right - BUTTONS)) return HTCAPTION;
		return HTCLIENT;
	}
	case WM_GETMINMAXINFO:
	{
		MINMAXINFO* clientSize = (MINMAXINFO*)lParam;
		POINT minSize{ 300, 300 };
		clientSize->ptMinTrackSize = minSize;
		break;
	}
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

//https://stackoverflow.com/a/25927081
void BindCrtHandlesToStdHandles(bool bindStdIn, bool bindStdOut, bool bindStdErr)
{
	// Re-initialize the C runtime "FILE" handles with clean handles bound to "nul". We do this because it has been
	// observed that the file number of our standard handle file objects can be assigned internally to a value of -2
	// when not bound to a valid target, which represents some kind of unknown internal invalid state. In this state our
	// call to "_dup2" fails, as it specifically tests to ensure that the target file number isn't equal to this value
	// before allowing the operation to continue. We can resolve this issue by first "re-opening" the target files to
	// use the "nul" device, which will place them into a valid state, after which we can redirect them to our target
	// using the "_dup2" function.
	if (bindStdIn)
	{
		FILE* dummyFile;
		freopen_s(&dummyFile, "nul", "r", stdin);
	}
	if (bindStdOut)
	{
		FILE* dummyFile;
		freopen_s(&dummyFile, "nul", "w", stdout);
	}
	if (bindStdErr)
	{
		FILE* dummyFile;
		freopen_s(&dummyFile, "nul", "w", stderr);
	}

	// Redirect unbuffered stdin from the current standard input handle
	if (bindStdIn)
	{
		HANDLE stdHandle = GetStdHandle(STD_INPUT_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE)
		{
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1)
			{
				FILE* file = _fdopen(fileDescriptor, "r");
				if (file != NULL)
				{
					int dup2Result = _dup2(_fileno(file), _fileno(stdin));
					if (dup2Result == 0)
					{
						setvbuf(stdin, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Redirect unbuffered stdout to the current standard output handle
	if (bindStdOut)
	{
		HANDLE stdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE)
		{
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1)
			{
				FILE* file = _fdopen(fileDescriptor, "w");
				if (file != NULL)
				{
					int dup2Result = _dup2(_fileno(file), _fileno(stdout));
					if (dup2Result == 0)
					{
						setvbuf(stdout, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Redirect unbuffered stderr to the current standard error handle
	if (bindStdErr)
	{
		HANDLE stdHandle = GetStdHandle(STD_ERROR_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE)
		{
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1)
			{
				FILE* file = _fdopen(fileDescriptor, "w");
				if (file != NULL)
				{
					int dup2Result = _dup2(_fileno(file), _fileno(stderr));
					if (dup2Result == 0)
					{
						setvbuf(stderr, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Clear the error state for each of the C++ standard stream objects. We need to do this, as attempts to access the
	// standard streams before they refer to a valid target will cause the iostream objects to enter an error state. In
	// versions of Visual Studio after 2005, this seems to always occur during startup regardless of whether anything
	// has been read from or written to the targets or not.
	if (bindStdIn)
	{
		std::wcin.clear();
		std::cin.clear();
	}
	if (bindStdOut)
	{
		std::wcout.clear();
		std::cout.clear();
	}
	if (bindStdErr)
	{
		std::wcerr.clear();
		std::cerr.clear();
	}
}
