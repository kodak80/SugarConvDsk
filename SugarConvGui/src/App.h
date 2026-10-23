#pragma once
//////////////////////////////////////////////////////////
// SugarConvGui - user interface (Dear ImGui, platform independent)
//////////////////////////////////////////////////////////

#include "Converter.h"

#include <functional>
#include <string>
#include <vector>

namespace scg
{

// Services the platform layer provides (file dialogs, opening folders)
struct PlatformHooks
{
   std::function<std::vector<std::string>()> open_files;                 // multi-select file dialog
   std::function<std::string(const std::string& title)> pick_folder;     // folder dialog ("" = cancelled)
   std::function<void(const std::string& path)> reveal;                  // show a file in Explorer
   std::function<void()> quit;
};

class App
{
public:
   explicit App(PlatformHooks hooks);
   ~App();

   // Call once after ImGui::CreateContext (persists the options in the ImGui .ini file)
   void RegisterSettings();
   // Draw one frame (inside ImGui::NewFrame / Render)
   void Frame();
   // Files or folders dropped on the window / given on the command line
   void AddPaths(const std::vector<std::string>& paths);

   Converter& converter() { return converter_; }
   Options& options() { return options_; }
   void SelectJob(int id) { selected_job_ = id; }
   void SelectTrack(int side, int track) { sel_side_ = side; sel_track_ = track; }

private:
   void DrawMenuBar();
   void DrawToolbar();
   void DrawOptions();
   void DrawQueue(const std::vector<Job>& jobs);
   void DrawTrackMap(const Job* job);
   void DrawTrackDetails(const DiskSummary& s);
   void DrawLog(const std::vector<Job>& jobs);
   void DrawAbout();

   PlatformHooks hooks_;
   Converter converter_;
   Options options_;
   int selected_job_ = 0;
   std::vector<int> multi_select_;
   int sel_side_ = -1, sel_track_ = -1;
   std::string live_log_;
   int live_job_ = 0;
   bool show_about_ = false;
   bool log_follow_ = true;
   char filter_buf_[128] = {0};
   char outdir_buf_[512] = {0};
   std::string status_;
   std::string options_sig_;
};

} // namespace scg
