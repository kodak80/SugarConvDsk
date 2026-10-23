//////////////////////////////////////////////////////////
// SugarConvGui - Windows front end (Win32 + Direct3D 11 + Dear ImGui)
//////////////////////////////////////////////////////////

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "App.h"

#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapchain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static bool g_occluded = false;
static UINT g_resize_w = 0, g_resize_h = 0;
static HWND g_hwnd = nullptr;
static scg::App* g_app = nullptr;

// ------------------------------------------------------------------------------------------------
// UTF-8 / UTF-16 (the manifest sets the process code page to UTF-8, so the core's fopen takes UTF-8)

static std::string ToUtf8(const std::wstring& w)
{
   if (w.empty()) return std::string();
   const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
   std::string s(n, '\0');
   WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
   return s;
}

static std::wstring ToWide(const std::string& s)
{
   if (s.empty()) return std::wstring();
   const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
   std::wstring w(n, L'\0');
   MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
   return w;
}

// ------------------------------------------------------------------------------------------------
// Dialogs

static std::vector<std::string> OpenFilesDialog()
{
   std::vector<std::string> out;
   IFileOpenDialog* dlg = nullptr;
   if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
   const COMDLG_FILTERSPEC types[] = {
      {L"Disk and flux dumps", L"*.scp;*.raw;*.ctr;*.ipf;*.hfe;*.dsk"},
      {L"SuperCard Pro (*.scp)", L"*.scp"},
      {L"KryoFlux stream (*.raw)", L"*.raw"},
      {L"CT-RAW / IPF (*.ctr;*.ipf)", L"*.ctr;*.ipf"},
      {L"All files", L"*.*"},
   };
   dlg->SetFileTypes(ARRAYSIZE(types), types);
   dlg->SetTitle(L"Add disk dumps");
   DWORD opts = 0;
   dlg->GetOptions(&opts);
   dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
   if (SUCCEEDED(dlg->Show(g_hwnd)))
   {
      IShellItemArray* items = nullptr;
      if (SUCCEEDED(dlg->GetResults(&items)))
      {
         DWORD count = 0;
         items->GetCount(&count);
         for (DWORD i = 0; i < count; i++)
         {
            IShellItem* item = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &item)))
            {
               PWSTR path = nullptr;
               if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
               {
                  out.push_back(ToUtf8(path));
                  CoTaskMemFree(path);
               }
               item->Release();
            }
         }
         items->Release();
      }
   }
   dlg->Release();
   return out;
}

static std::string PickFolderDialog(const std::string& title)
{
   std::string out;
   IFileOpenDialog* dlg = nullptr;
   if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
   DWORD opts = 0;
   dlg->GetOptions(&opts);
   dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
   const std::wstring wtitle = ToWide(title);
   dlg->SetTitle(wtitle.c_str());
   if (SUCCEEDED(dlg->Show(g_hwnd)))
   {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dlg->GetResult(&item)))
      {
         PWSTR path = nullptr;
         if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
         {
            out = ToUtf8(path);
            CoTaskMemFree(path);
         }
         item->Release();
      }
   }
   dlg->Release();
   return out;
}

static void RevealInExplorer(const std::string& path)
{
   const std::wstring args = L"/select,\"" + ToWide(path) + L"\"";
   ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------------------------------------
// Direct3D

static void CreateRenderTarget()
{
   ID3D11Texture2D* back = nullptr;
   g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
   if (back)
   {
      g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
      back->Release();
   }
}

static void CleanupRenderTarget()
{
   if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDeviceD3D(HWND hwnd)
{
   DXGI_SWAP_CHAIN_DESC sd = {};
   sd.BufferCount = 2;
   sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   sd.BufferDesc.RefreshRate.Numerator = 60;
   sd.BufferDesc.RefreshRate.Denominator = 1;
   sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
   sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
   sd.OutputWindow = hwnd;
   sd.SampleDesc.Count = 1;
   sd.Windowed = TRUE;
   sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
   D3D_FEATURE_LEVEL level;
   const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
   HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swapchain, &g_device, &level, &g_context);
   if (res == DXGI_ERROR_UNSUPPORTED) // no GPU : software rasteriser
      res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                          &g_swapchain, &g_device, &level, &g_context);
   if (res != S_OK) return false;
   CreateRenderTarget();
   return true;
}

static void CleanupDeviceD3D()
{
   CleanupRenderTarget();
   if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
   if (g_context) { g_context->Release(); g_context = nullptr; }
   if (g_device) { g_device->Release(); g_device = nullptr; }
}

// ------------------------------------------------------------------------------------------------

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
   if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
   switch (msg)
   {
   case WM_SIZE:
      if (wparam == SIZE_MINIMIZED) return 0;
      g_resize_w = static_cast<UINT>(LOWORD(lparam));
      g_resize_h = static_cast<UINT>(HIWORD(lparam));
      return 0;
   case WM_GETMINMAXINFO:
   {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
      mmi->ptMinTrackSize.x = 900;
      mmi->ptMinTrackSize.y = 600;
      return 0;
   }
   case WM_DPICHANGED:
   {
      const RECT* r = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
   }
   case WM_DROPFILES:
   {
      HDROP drop = reinterpret_cast<HDROP>(wparam);
      const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      std::vector<std::string> paths;
      for (UINT i = 0; i < n; i++)
      {
         const UINT len = DragQueryFileW(drop, i, nullptr, 0);
         std::wstring w(len, L'\0');
         DragQueryFileW(drop, i, &w[0], len + 1);
         paths.push_back(ToUtf8(w));
      }
      DragFinish(drop);
      if (g_app) g_app->AddPaths(paths);
      return 0;
   }
   case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0; // no ALT menu
      break;
   case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
   }
   return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void LoadFonts(float scale)
{
   ImGuiIO& io = ImGui::GetIO();
   wchar_t windir[MAX_PATH] = {0};
   GetWindowsDirectoryW(windir, MAX_PATH);
   const std::string fonts = ToUtf8(windir) + "\\Fonts\\";
   ImFontConfig cfg;
   cfg.OversampleH = 2;
   ImFont* ui = nullptr;
   if (GetFileAttributesA((fonts + "segoeui.ttf").c_str()) != INVALID_FILE_ATTRIBUTES)
      ui = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 17.0f * scale, &cfg);
   if (ui == nullptr)
   {
      ImFontConfig def;
      def.SizePixels = 13.0f * scale;
      io.Fonts->AddFontDefault(&def);
   }
   // second font : log text
   ImFont* mono = nullptr;
   if (GetFileAttributesA((fonts + "consola.ttf").c_str()) != INVALID_FILE_ATTRIBUTES)
      mono = io.Fonts->AddFontFromFileTTF((fonts + "consola.ttf").c_str(), 15.0f * scale, &cfg);
   if (mono == nullptr)
   {
      ImFontConfig def;
      def.SizePixels = 13.0f * scale;
      io.Fonts->AddFontDefault(&def);
   }
}

static void ApplyStyle(float scale)
{
   ImGui::StyleColorsDark();
   ImGuiStyle& s = ImGui::GetStyle();
   s.FrameRounding = 4.0f;
   s.GrabRounding = 4.0f;
   s.ChildRounding = 4.0f;
   s.PopupRounding = 4.0f;
   s.WindowPadding = ImVec2(10, 8);
   s.FramePadding = ImVec2(8, 4);
   s.ItemSpacing = ImVec2(8, 6);
   s.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
   s.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
   s.ScaleAllSizes(scale);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
   ImGui_ImplWin32_EnableDpiAwareness();
   CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

   WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, instance, LoadIconW(instance, L"IDI_APP"), LoadCursor(nullptr, IDC_ARROW),
                     nullptr, nullptr, L"SugarConvGui", nullptr};
   RegisterClassExW(&wc);
   const POINT origin = {0, 0};
   const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY));
   g_hwnd = CreateWindowW(wc.lpszClassName, L"SugarConvDsk", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          static_cast<int>(1360 * scale), static_cast<int>(860 * scale), nullptr, nullptr, instance, nullptr);
   if (!CreateDeviceD3D(g_hwnd))
   {
      CleanupDeviceD3D();
      MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"SugarConvDsk", MB_ICONERROR);
      return 1;
   }
   DragAcceptFiles(g_hwnd, TRUE);
   ShowWindow(g_hwnd, SW_SHOWDEFAULT);
   UpdateWindow(g_hwnd);

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
   // settings next to the user's other app data
   static std::string ini;
   {
      PWSTR local = nullptr;
      if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
      {
         std::wstring dir = std::wstring(local) + L"\\SugarConvDsk";
         CreateDirectoryW(dir.c_str(), nullptr);
         ini = ToUtf8(dir) + "\\SugarConvGui.ini";
         CoTaskMemFree(local);
         io.IniFilename = ini.c_str();
      }
   }

   ApplyStyle(scale);
   LoadFonts(scale);
   ImGui_ImplWin32_Init(g_hwnd);
   ImGui_ImplDX11_Init(g_device, g_context);

   scg::PlatformHooks hooks;
   hooks.open_files = OpenFilesDialog;
   hooks.pick_folder = PickFolderDialog;
   hooks.reveal = RevealInExplorer;
   hooks.quit = [] { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); };
   scg::App app(hooks);
   g_app = &app;
   app.RegisterSettings();
   ImGui::LoadIniSettingsFromDisk(io.IniFilename); // the handler is registered after CreateContext

   // paths given on the command line (e.g. files dropped on the .exe)
   {
      int argc = 0;
      LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
      std::vector<std::string> paths;
      for (int i = 1; argv && i < argc; i++) paths.push_back(ToUtf8(argv[i]));
      if (argv) LocalFree(argv);
      if (!paths.empty()) app.AddPaths(paths);
   }

   const ImVec4 clear(0.10f, 0.11f, 0.13f, 1.0f);
   bool done = false;
   while (!done)
   {
      // sleep while nothing happens (no conversion running, no input)
      if (!app.converter().Busy()) MsgWaitForMultipleObjects(0, nullptr, FALSE, 250, QS_ALLINPUT);
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
      {
         TranslateMessage(&msg);
         DispatchMessageW(&msg);
         if (msg.message == WM_QUIT) done = true;
      }
      if (done) break;
      if (g_occluded && g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
      {
         Sleep(10);
         continue;
      }
      g_occluded = false;
      if (g_resize_w != 0 && g_resize_h != 0)
      {
         CleanupRenderTarget();
         g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
         g_resize_w = g_resize_h = 0;
         CreateRenderTarget();
      }

      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();
      app.Frame();
      ImGui::Render();
      const float c[4] = {clear.x, clear.y, clear.z, clear.w};
      g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
      g_context->ClearRenderTargetView(g_rtv, c);
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
      const HRESULT hr = g_swapchain->Present(1, 0);
      g_occluded = (hr == DXGI_STATUS_OCCLUDED);
   }

   g_app = nullptr;
   ImGui_ImplDX11_Shutdown();
   ImGui_ImplWin32_Shutdown();
   ImGui::DestroyContext();
   CleanupDeviceD3D();
   DestroyWindow(g_hwnd);
   UnregisterClassW(wc.lpszClassName, wc.hInstance);
   CoUninitialize();
   return 0;
}
