//////////////////////////////////////////////////////////
// SugarConvGui - SDL2 front end for development on Linux (not shipped)
//
// Runs the same App as the Windows build. Headless checks:
//   SDL_VIDEODRIVER=dummy SugarConvGuiSDL --shot out.bmp [--analyse|--convert] [--track S T] [--out DIR] paths...
// renders after the conversions finish and saves a screenshot.
//////////////////////////////////////////////////////////

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "App.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
   std::string shot, outdir;
   bool analyse = false, convert = false;
   int track_side = -1, track = -1, width = 1360, height = 860, format = -1;
   std::vector<std::string> paths;
   for (int i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
      else if (!strcmp(argv[i], "--analyse")) analyse = true;
      else if (!strcmp(argv[i], "--convert")) convert = true;
      else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
      else if (!strcmp(argv[i], "--format") && i + 1 < argc) format = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--size") && i + 2 < argc) { width = atoi(argv[++i]); height = atoi(argv[++i]); }
      else if (!strcmp(argv[i], "--track") && i + 2 < argc) { track_side = atoi(argv[++i]); track = atoi(argv[++i]); }
      else paths.push_back(argv[i]);
   }

   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
   {
      printf("SDL_Init: %s\n", SDL_GetError());
      return 1;
   }
   SDL_Window* window = SDL_CreateWindow("SugarConvDsk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                         SDL_WINDOW_RESIZABLE);
   SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, shot.empty() ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE);
   if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.IniFilename = shot.empty() ? "SugarConvGui.ini" : nullptr;
   ImGui::StyleColorsDark();
   ImGuiStyle& s = ImGui::GetStyle();
   s.FrameRounding = s.GrabRounding = s.ChildRounding = 4.0f;
   s.WindowPadding = ImVec2(10, 8);
   s.FramePadding = ImVec2(8, 4);
   s.ItemSpacing = ImVec2(8, 6);
   s.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
   s.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
   const char* ttf = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
   const char* mono = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
   if (FILE* f = fopen(ttf, "rb")) { fclose(f); io.Fonts->AddFontFromFileTTF(ttf, 16.0f); } else io.Fonts->AddFontDefault();
   if (FILE* f = fopen(mono, "rb")) { fclose(f); io.Fonts->AddFontFromFileTTF(mono, 14.0f); } else io.Fonts->AddFontDefault();
   ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
   ImGui_ImplSDLRenderer2_Init(renderer);

   bool done = false;
   scg::PlatformHooks hooks;
   hooks.quit = [&] { done = true; };
   scg::App app(hooks);
   app.RegisterSettings();
   if (format >= 0) app.options().format = static_cast<scg::OutFormat>(format);
   if (!outdir.empty()) { app.options().output_next_to_source = false; app.options().output_dir = outdir; }
   if (!paths.empty()) app.AddPaths(paths);
   if (analyse || convert) app.converter().Start(app.options(), {}, analyse);

   int frame = 0, idle_frames = 0;
   while (!done)
   {
      SDL_Event e;
      while (SDL_PollEvent(&e))
      {
         ImGui_ImplSDL2_ProcessEvent(&e);
         if (e.type == SDL_QUIT) done = true;
         if (e.type == SDL_DROPFILE) { app.AddPaths({e.drop.file}); SDL_free(e.drop.file); }
      }
      ImGui_ImplSDLRenderer2_NewFrame();
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();
      app.Frame();
      ImGui::Render();
      SDL_SetRenderDrawColor(renderer, 26, 28, 33, 255);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

      frame++;
      if (!shot.empty())
      {
         // wait for the conversions, then a few frames to settle, then save
         if (app.converter().Busy()) idle_frames = 0;
         else idle_frames++;
         if (idle_frames == 3 && track_side >= 0) app.SelectTrack(track_side, track);
         if (idle_frames > 8 && frame > 10)
         {
            int w, h;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
            SDL_SaveBMP(surf, shot.c_str());
            SDL_FreeSurface(surf);
            done = true;
         }
         SDL_Delay(app.converter().Busy() ? 20 : 1);
      }
      SDL_RenderPresent(renderer);
   }

   ImGui_ImplSDLRenderer2_Shutdown();
   ImGui_ImplSDL2_Shutdown();
   ImGui::DestroyContext();
   SDL_DestroyRenderer(renderer);
   SDL_DestroyWindow(window);
   SDL_Quit();
   return 0;
}
