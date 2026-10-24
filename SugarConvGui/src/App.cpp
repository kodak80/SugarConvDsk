//////////////////////////////////////////////////////////
// SugarConvGui - user interface
//////////////////////////////////////////////////////////

#include "App.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace scg
{

#ifndef SCG_VERSION
#define SCG_VERSION "dev"
#endif

static ImU32 KindColour(TrackKind k)
{
   switch (k)
   {
   case TrackKind::Absent: return IM_COL32(60, 64, 72, 255);
   case TrackKind::Unformatted: return IM_COL32(110, 116, 128, 255);
   case TrackKind::Ok: return IM_COL32(88, 170, 110, 255);
   case TrackKind::CrcError: return IM_COL32(214, 72, 72, 255);
   case TrackKind::Weak: return IM_COL32(226, 196, 70, 255);
   case TrackKind::PhaseSlip: return IM_COL32(230, 140, 60, 255);
   case TrackKind::DensityKey: return IM_COL32(80, 150, 235, 255);
   case TrackKind::Stream: return IM_COL32(170, 110, 220, 255);
   default: return IM_COL32(255, 0, 255, 255);
   }
}

static const char* StatusText(const Job& j)
{
   switch (j.status)
   {
   case JobStatus::Queued: return "Queued";
   case JobStatus::Running: return "Running";
   case JobStatus::Done: return j.analyse_only ? "Analysed" : "Done";
   case JobStatus::Failed: return "Failed";
   }
   return "";
}

static ImVec4 StatusColour(const Job& j)
{
   switch (j.status)
   {
   case JobStatus::Queued: return ImVec4(0.75f, 0.75f, 0.78f, 1);
   case JobStatus::Running: return ImVec4(0.45f, 0.70f, 1.0f, 1);
   case JobStatus::Done: return ImVec4(0.45f, 0.82f, 0.52f, 1);
   case JobStatus::Failed: return ImVec4(0.95f, 0.40f, 0.40f, 1);
   }
   return ImVec4(1, 1, 1, 1);
}

static void HelpMarker(const char* text)
{
   ImGui::SameLine();
   ImGui::TextDisabled("(?)");
   if (ImGui::BeginItemTooltip())
   {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
   }
}

// ------------------------------------------------------------------------------------------------

App::App(PlatformHooks hooks) : hooks_(std::move(hooks))
{
}

App::~App() = default;

void App::RegisterSettings()
{
   ImGuiSettingsHandler h;
   h.TypeName = "SugarConv";
   h.TypeHash = ImHashStr("SugarConv");
   h.UserData = this;
   h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, const char*) -> void* { return handler->UserData; };
   h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
   {
      App* app = static_cast<App*>(entry);
      Options& o = app->options_;
      int v = 0;
      char buf[512] = {0};
      if (sscanf(line, "Format=%d", &v) == 1 && v >= 0 && v < static_cast<int>(OutFormat::Count)) o.format = static_cast<OutFormat>(v);
      else if (sscanf(line, "Platform=%d", &v) == 1) o.platform = std::clamp(v, 0, 2);
      else if (sscanf(line, "Sides=%d", &v) == 1) o.sides = std::clamp(v, 0, 2);
      else if (sscanf(line, "Speedlock10=%d", &v) == 1) o.speedlock10 = v != 0;
      else if (sscanf(line, "IpfRaw=%d", &v) == 1) o.ipf_raw = v != 0;
      else if (sscanf(line, "ScpNoFlux=%d", &v) == 1) o.scp_noflux = v != 0;
      else if (sscanf(line, "HfeV1=%d", &v) == 1) o.hfe_v1 = v != 0;
      else if (sscanf(line, "NextToSource=%d", &v) == 1) o.output_next_to_source = v != 0;
      else if (sscanf(line, "Recursive=%d", &v) == 1) o.recursive = v != 0;
      else if (strncmp(line, "OutputDir=", 10) == 0)
      {
         o.output_dir = line + 10;
         snprintf(app->outdir_buf_, sizeof(app->outdir_buf_), "%s", o.output_dir.c_str());
      }
      else if (strncmp(line, "Filter=", 7) == 0)
      {
         o.filter = line + 7;
         snprintf(app->filter_buf_, sizeof(app->filter_buf_), "%s", o.filter.c_str());
      }
      (void)buf;
   };
   h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out)
   {
      const App* app = static_cast<const App*>(handler->UserData);
      const Options& o = app->options_;
      out->appendf("[%s][Options]\n", handler->TypeName);
      out->appendf("Format=%d\nPlatform=%d\nSides=%d\nSpeedlock10=%d\nIpfRaw=%d\nScpNoFlux=%d\nHfeV1=%d\n",
                   static_cast<int>(o.format), o.platform, o.sides, o.speedlock10, o.ipf_raw, o.scp_noflux, o.hfe_v1);
      out->appendf("NextToSource=%d\nRecursive=%d\nOutputDir=%s\nFilter=%s\n\n", o.output_next_to_source, o.recursive,
                   o.output_dir.c_str(), o.filter.c_str());
   };
   ImGui::AddSettingsHandler(&h);
}

void App::AddPaths(const std::vector<std::string>& paths)
{
   const auto sources = CollectSources(paths, options_);
   int first = 0;
   for (const auto& s : sources)
   {
      const int id = converter_.Add(s);
      if (!first) first = id;
   }
   if (first && !selected_job_) selected_job_ = first;
   char buf[128];
   snprintf(buf, sizeof(buf), sources.empty() ? "No supported dumps found" : "Added %d dump(s)",
            static_cast<int>(sources.size()));
   status_ = buf;
}

// ------------------------------------------------------------------------------------------------

void App::Frame()
{
   const ImGuiViewport* vp = ImGui::GetMainViewport();
   ImGui::SetNextWindowPos(vp->WorkPos);
   ImGui::SetNextWindowSize(vp->WorkSize);
   ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
   ImGui::Begin("SugarConvMain", nullptr, flags);
   ImGui::PopStyleVar();

   // follow the running job's output
   const std::string chunk = converter_.TakeLiveLog();
   if (converter_.CurrentJob() != live_job_)
   {
      live_job_ = converter_.CurrentJob();
      live_log_.clear();
      if (live_job_) selected_job_ = live_job_;
   }
   live_log_ += chunk;

   const std::vector<Job> jobs = converter_.Snapshot();

   DrawMenuBar();
   DrawToolbar();
   ImGui::Separator();

   const float options_w = ImGui::GetFontSize() * 21.0f;
   ImGui::BeginChild("options", ImVec2(options_w, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
   DrawOptions();
   ImGui::EndChild();
   ImGui::SameLine();

   ImGui::BeginChild("main", ImVec2(0, 0));
   {
      ImGui::BeginChild("queue", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.30f),
                        ImGuiChildFlags_ResizeY | ImGuiChildFlags_Borders);
      DrawQueue(jobs);
      ImGui::EndChild();

      const Job* sel = nullptr;
      for (auto& j : jobs)
         if (j.id == selected_job_) sel = &j;

      ImGui::BeginChild("map", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.68f),
                        ImGuiChildFlags_ResizeY | ImGuiChildFlags_Borders);
      DrawTrackMap(sel);
      ImGui::EndChild();

      ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
      DrawLog(jobs);
      ImGui::EndChild();
   }
   ImGui::EndChild();

   if (show_about_) DrawAbout();
   ImGui::End();

   // save the options when they change
   char sig[1024];
   snprintf(sig, sizeof(sig), "%d%d%d%d%d%d%d%d%d|%s|%s", static_cast<int>(options_.format), options_.platform,
            options_.sides, options_.speedlock10, options_.ipf_raw, options_.scp_noflux, options_.hfe_v1,
            options_.output_next_to_source, options_.recursive, options_.output_dir.c_str(), options_.filter.c_str());
   if (options_sig_ != sig)
   {
      if (!options_sig_.empty()) ImGui::MarkIniSettingsDirty();
      options_sig_ = sig;
   }
}

void App::DrawMenuBar()
{
   if (!ImGui::BeginMenuBar()) return;
   if (ImGui::BeginMenu("File"))
   {
      if (ImGui::MenuItem("Add files...", "Ctrl+O") && hooks_.open_files) AddPaths(hooks_.open_files());
      if (ImGui::MenuItem("Add folder...", "Ctrl+Shift+O") && hooks_.pick_folder)
      {
         const std::string f = hooks_.pick_folder("Folder of flux dumps");
         if (!f.empty()) AddPaths({f});
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Remove finished")) converter_.ClearFinished();
      if (ImGui::MenuItem("Clear all", nullptr, false, !converter_.Busy()))
      {
         converter_.ClearAll();
         selected_job_ = 0;
         multi_select_.clear();
         sel_side_ = sel_track_ = -1;
         live_log_.clear();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit") && hooks_.quit) hooks_.quit();
      ImGui::EndMenu();
   }
   if (ImGui::BeginMenu("Help"))
   {
      if (ImGui::MenuItem("About SugarConvDsk")) show_about_ = true;
      ImGui::EndMenu();
   }
   ImGui::EndMenuBar();

   // shortcuts
   if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O) && hooks_.open_files) AddPaths(hooks_.open_files());
   if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O) && hooks_.pick_folder)
   {
      const std::string f = hooks_.pick_folder("Folder of flux dumps");
      if (!f.empty()) AddPaths({f});
   }
}

void App::DrawToolbar()
{
   const bool busy = converter_.Busy();
   if (ImGui::Button("Add files...") && hooks_.open_files) AddPaths(hooks_.open_files());
   ImGui::SameLine();
   if (ImGui::Button("Add folder...") && hooks_.pick_folder)
   {
      const std::string f = hooks_.pick_folder("Folder of flux dumps");
      if (!f.empty()) AddPaths({f});
   }
   ImGui::SameLine();
   ImGui::TextDisabled("|");
   ImGui::SameLine();

   std::vector<int> selection = multi_select_;
   if (selection.empty() && selected_job_) selection.push_back(selected_job_);

   ImGui::BeginDisabled(busy);
   ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.30f, 1));
   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.56f, 0.37f, 1));
   char label[64];
   snprintf(label, sizeof(label), "Convert all to %s", OutFormatName(options_.format));
   if (ImGui::Button(label)) converter_.Start(options_, {}, false);
   ImGui::PopStyleColor(2);
   ImGui::SameLine();
   ImGui::BeginDisabled(selection.empty());
   if (ImGui::Button("Convert selected")) converter_.Start(options_, selection, false);
   ImGui::SameLine();
   if (ImGui::Button("Analyse selected")) converter_.Start(options_, selection, true);
   ImGui::EndDisabled();
   ImGui::EndDisabled();
   ImGui::SameLine();
   ImGui::BeginDisabled(!busy);
   if (ImGui::Button("Stop after current")) converter_.StopAfterCurrent();
   ImGui::EndDisabled();
   ImGui::SameLine();
   ImGui::TextDisabled("|");
   ImGui::SameLine();
   ImGui::BeginDisabled(busy || converter_.Snapshot().empty());
   if (ImGui::Button("Clear all"))
   {
      converter_.ClearAll();
      selected_job_ = 0;
      multi_select_.clear();
      sel_side_ = sel_track_ = -1;
      live_log_.clear();
      status_ = "List cleared";
   }
   ImGui::EndDisabled();
   ImGui::SetItemTooltip("Remove every disk from the list (files on disk are not touched)");
   if (!status_.empty())
   {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", status_.c_str());
   }
}

void App::DrawOptions()
{
   ImGui::SeparatorText("Output format");
   for (int i = 0; i < static_cast<int>(OutFormat::Count); i++)
   {
      const OutFormat f = static_cast<OutFormat>(i);
      if (ImGui::RadioButton(OutFormatName(f), options_.format == f)) options_.format = f;
      ImGui::SameLine(ImGui::GetFontSize() * 5.4f);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", OutFormatDescription(f));
      ImGui::PopTextWrapPos();
   }

   if (!ImGui::IsAnyItemActive())
   {
      if (options_.output_dir != outdir_buf_) snprintf(outdir_buf_, sizeof(outdir_buf_), "%s", options_.output_dir.c_str());
      if (options_.filter != filter_buf_) snprintf(filter_buf_, sizeof(filter_buf_), "%s", options_.filter.c_str());
   }
   ImGui::SeparatorText("Save to");
   if (ImGui::RadioButton("Next to each dump", options_.output_next_to_source)) options_.output_next_to_source = true;
   if (ImGui::RadioButton("This folder:", !options_.output_next_to_source)) options_.output_next_to_source = false;
   ImGui::BeginDisabled(options_.output_next_to_source);
   ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4.0f);
   if (ImGui::InputText("##outdir", outdir_buf_, sizeof(outdir_buf_))) options_.output_dir = outdir_buf_;
   ImGui::SameLine();
   if (ImGui::Button("Browse") && hooks_.pick_folder)
   {
      const std::string f = hooks_.pick_folder("Output folder");
      if (!f.empty())
      {
         options_.output_dir = f;
         snprintf(outdir_buf_, sizeof(outdir_buf_), "%s", f.c_str());
      }
   }
   ImGui::EndDisabled();

   ImGui::SeparatorText("Disk");
   const char* platforms[] = {"Guess from the dump", "Atari ST", "Amstrad CPC"};
   ImGui::SetNextItemWidth(-1);
   ImGui::Combo("##platform", &options_.platform, platforms, 3);
   const char* sides[] = {"Both sides", "Side 1 only", "Side 2 only"};
   ImGui::SetNextItemWidth(-1);
   ImGui::Combo("##sides", &options_.sides, sides, 3);

   ImGui::SeparatorText("Protections");
   ImGui::Checkbox("Speedlock key +10% / -10%", &options_.speedlock10);
   HelpMarker("Writes Atari ST Speedlock-style speed keys as IPF density type 6 (slow patch +10%, fast patch -10%), "
              "the layout some existing images use. Off (default): type 7, slow patch +5% as measured on the original "
              "disks, which Dragon's Breath needs.");

   ImGui::SeparatorText("Format options");
   ImGui::BeginDisabled(options_.format != OutFormat::IPF);
   ImGui::Checkbox("IPF: raw cells (larger)", &options_.ipf_raw);
   HelpMarker("Stores every track as MFM cells, like older SugarConvDsk versions. Off (default): sectors are stored "
              "as decoded bytes with generated gaps, like SPS images, about half the size.");
   ImGui::EndDisabled();
   ImGui::BeginDisabled(options_.format != OutFormat::SCP);
   ImGui::Checkbox("SCP: weak bits as no-flux gaps", &options_.scp_noflux);
   HelpMarker("For writing the SCP back to a real floppy: weak areas become gaps without flux transitions (one "
              "revolution), which a drive reads as noise. Off (default): random data in each of 3 revolutions, for "
              "emulators.");
   ImGui::EndDisabled();
   ImGui::BeginDisabled(options_.format != OutFormat::HFE);
   ImGui::Checkbox("HFE: version 1", &options_.hfe_v1);
   HelpMarker("Original HFE format (HxC). It cannot keep weak bits or sector speed changes, so most protections are "
              "lost. Off (default): HFE v3, which FlashFloppy plays.");
   ImGui::EndDisabled();

   ImGui::SeparatorText("Adding folders");
   ImGui::Checkbox("Include sub-folders", &options_.recursive);
   ImGui::SetNextItemWidth(-1);
   if (ImGui::InputTextWithHint("##filter", "Filter, e.g. *.scp", filter_buf_, sizeof(filter_buf_)))
      options_.filter = filter_buf_;
   ImGui::TextDisabled("Drop files or folders anywhere.\nFolders add SCP, KryoFlux and CT-RAW\ndumps; a KryoFlux set counts once.");
}

void App::DrawQueue(const std::vector<Job>& jobs)
{
   if (jobs.empty())
   {
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const char* t1 = "Drop SCP, KryoFlux, CT-RAW, IPF or HFE dumps here";
      const char* t2 = "or use Add files... / Add folder...";
      ImGui::SetCursorPos(ImVec2((avail.x - ImGui::CalcTextSize(t1).x) * 0.5f, avail.y * 0.40f));
      ImGui::TextDisabled("%s", t1);
      ImGui::SetCursorPosX((avail.x - ImGui::CalcTextSize(t2).x) * 0.5f);
      ImGui::TextDisabled("%s", t2);
      return;
   }
   const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
   if (!ImGui::BeginTable("jobs", 5, tf)) return;
   ImGui::TableSetupScrollFreeze(0, 1);
   ImGui::TableSetupColumn("Disk", ImGuiTableColumnFlags_WidthStretch, 3.0f);
   ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.2f);
   ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthStretch, 3.2f);
   ImGui::TableSetupColumn("Output", ImGuiTableColumnFlags_WidthStretch, 2.4f);
   ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.5f);
   ImGui::TableHeadersRow();

   int remove_id = 0;
   for (const auto& j : jobs)
   {
      ImGui::PushID(j.id);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool in_multi = std::find(multi_select_.begin(), multi_select_.end(), j.id) != multi_select_.end();
      const bool is_sel = (j.id == selected_job_) || in_multi;
      if (ImGui::Selectable(j.name.c_str(), is_sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
      {
         if (ImGui::GetIO().KeyCtrl)
         {
            if (in_multi) multi_select_.erase(std::find(multi_select_.begin(), multi_select_.end(), j.id));
            else
            {
               if (multi_select_.empty() && selected_job_ && selected_job_ != j.id) multi_select_.push_back(selected_job_);
               multi_select_.push_back(j.id);
            }
         }
         else
         {
            multi_select_.clear();
         }
         selected_job_ = j.id;
         sel_side_ = sel_track_ = -1;
      }
      if (ImGui::BeginItemTooltip())
      {
         ImGui::TextUnformatted(j.source.c_str());
         ImGui::EndTooltip();
      }
      ImGui::TableNextColumn();
      if (j.status == JobStatus::Running)
      {
         char buf[32];
         snprintf(buf, sizeof(buf), "%d%%", j.progress);
         ImGui::ProgressBar(j.progress / 100.0f, ImVec2(-1, 0), buf);
      }
      else
      {
         ImGui::TextColored(StatusColour(j), "%s", StatusText(j));
      }
      ImGui::TableNextColumn();
      if (j.summary)
      {
         ImGui::TextUnformatted(j.summary->protections.c_str());
         ImGui::SetItemTooltip("%s", j.summary->protections.c_str());
      }
      else if (!j.message.empty())
         ImGui::TextDisabled("%s", j.message.c_str());
      ImGui::TableNextColumn();
      if (!j.output.empty())
      {
         const size_t slash = j.output.find_last_of("/\\");
         ImGui::TextUnformatted(slash == std::string::npos ? j.output.c_str() : j.output.c_str() + slash + 1);
         if (hooks_.reveal && ImGui::BeginItemTooltip())
         {
            ImGui::Text("%s\n(double-click to show in Explorer)", j.output.c_str());
            ImGui::EndTooltip();
         }
         if (hooks_.reveal && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) hooks_.reveal(j.output);
      }
      else if (j.status == JobStatus::Failed)
      {
         ImGui::TextColored(StatusColour(j), "%s", j.message.c_str());
      }
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(j.status == JobStatus::Running);
      if (ImGui::SmallButton("x")) remove_id = j.id;
      ImGui::SetItemTooltip("Remove from the list");
      ImGui::EndDisabled();
      ImGui::PopID();
   }
   ImGui::EndTable();
   if (remove_id)
   {
      converter_.Remove(remove_id);
      if (selected_job_ == remove_id) selected_job_ = 0;
      multi_select_.erase(std::remove(multi_select_.begin(), multi_select_.end(), remove_id), multi_select_.end());
   }
}

void App::DrawTrackMap(const Job* job)
{
   if (job == nullptr || !job->summary)
   {
      ImGui::TextDisabled(job == nullptr ? "Track map: select a disk in the list."
                                         : "Track map: convert or analyse this disk to see its tracks.");
      return;
   }
   const DiskSummary& s = *job->summary;
   ImGui::Text("%s", job->name.c_str());
   ImGui::SameLine();
   ImGui::TextDisabled("%s, %s, %d side(s), %d tracks", s.source_format.c_str(), s.platform.c_str(), s.sides, s.tracks);

   const float details_min = ImGui::GetFontSize() * 21.0f;
   float cell = std::max(14.0f, ImGui::GetFontSize() * 1.25f);
   {
      // shrink the cells so the two grids and the details fit side by side
      const float label_w0 = ImGui::CalcTextSize("00").x + 4;
      const float avail = ImGui::GetContentRegionAvail().x - details_min - 2 * label_w0 - 4.0f * cell;
      const float fit = avail / (20 * 1.12f);
      cell = std::max(11.0f, std::min(cell, fit));
   }
   const float gap = 2.0f;
   const int per_row = 10;
   const int rows = (std::max(s.tracks, 1) + per_row - 1) / per_row;
   ImDrawList* dl = ImGui::GetWindowDrawList();

   ImGui::BeginGroup();
   ImGui::BeginGroup();
   for (int side = 0; side < 2; side++)
   {
      if (side) ImGui::SameLine(0, cell * 1.5f);
      ImGui::BeginGroup();
      ImGui::Text("Side %d", side);
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float label_w = ImGui::CalcTextSize("00").x + 4;
      // column digits
      for (int c = 0; c < per_row; c++)
      {
         char d[8];
         snprintf(d, sizeof(d), "%d", c);
         dl->AddText(ImVec2(origin.x + label_w + c * (cell + gap) + (cell - ImGui::CalcTextSize(d).x) * 0.5f, origin.y),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), d);
      }
      const float top = origin.y + ImGui::GetTextLineHeight() + 2;
      for (int r = 0; r < rows; r++)
      {
         char d[8];
         snprintf(d, sizeof(d), "%d", r);
         dl->AddText(ImVec2(origin.x, top + r * (cell + gap) + (cell - ImGui::GetTextLineHeight()) * 0.5f),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), d);
         for (int c = 0; c < per_row; c++)
         {
            const int tr = r * per_row + c;
            if (tr >= s.tracks) break;
            const TrackInfo& ti = s.track[side][tr];
            const ImVec2 a(origin.x + label_w + c * (cell + gap), top + r * (cell + gap));
            const ImVec2 b(a.x + cell, a.y + cell);
            dl->AddRectFilled(a, b, KindColour(ti.kind), 2.0f);
            if (side == sel_side_ && tr == sel_track_) dl->AddRect(a, b, IM_COL32(255, 255, 255, 255), 2.0f, 0, 2.0f);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(side * 1000 + tr);
            if (ImGui::InvisibleButton("t", ImVec2(cell, cell)))
            {
               sel_side_ = side;
               sel_track_ = tr;
            }
            if (ImGui::BeginItemTooltip())
            {
               ImGui::Text("Track %02d.%d - %s", tr, side, TrackKindName(ti.kind));
               if (ti.kind != TrackKind::Absent)
               {
                  ImGui::TextDisabled("%u cells, %d sector(s)", ti.cells, ti.sectors);
                  if (!ti.notes.empty()) ImGui::TextUnformatted(ti.notes.c_str());
               }
               ImGui::EndTooltip();
            }
            ImGui::PopID();
         }
      }
      ImGui::SetCursorScreenPos(ImVec2(origin.x, top + rows * (cell + gap)));
      ImGui::Dummy(ImVec2(label_w + per_row * (cell + gap), 0));
      ImGui::EndGroup();
   }
   ImGui::EndGroup();
   // legend, wrapped to the width of the two grids
   ImGui::Spacing();
   const float grids_w = ImGui::GetItemRectSize().x;
   const float legend_x0 = ImGui::GetCursorScreenPos().x;
   const float sz = ImGui::GetTextLineHeight() * 0.8f;
   for (int k = static_cast<int>(TrackKind::Unformatted); k < static_cast<int>(TrackKind::Count); k++)
   {
      const char* label = TrackKindName(static_cast<TrackKind>(k));
      const float item_w = sz + 6 + ImGui::CalcTextSize(label).x;
      if (k > static_cast<int>(TrackKind::Unformatted))
      {
         ImGui::SameLine(0, 14);
         if (ImGui::GetCursorScreenPos().x + item_w > legend_x0 + grids_w) ImGui::NewLine();
      }
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float y = p.y + (ImGui::GetTextLineHeight() - sz) * 0.5f;
      dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + sz, y + sz), KindColour(static_cast<TrackKind>(k)), 2.0f);
      ImGui::Dummy(ImVec2(sz + 2, ImGui::GetTextLineHeight()));
      ImGui::SameLine(0, 4);
      ImGui::TextUnformatted(label);
   }
   ImGui::EndGroup();

   ImGui::SameLine(0, cell * 1.5f);
   ImGui::BeginChild("details", ImVec2(0, 0));
   DrawTrackDetails(s);
   ImGui::EndChild();
}

void App::DrawTrackDetails(const DiskSummary& s)
{
   if (sel_side_ < 0 || sel_track_ < 0 || sel_track_ >= s.tracks)
   {
      ImGui::TextDisabled("Click a track for its sectors.");
      return;
   }
   const TrackInfo& ti = s.track[sel_side_][sel_track_];
   ImGui::Text("Track %02d.%d", sel_track_, sel_side_);
   ImGui::SameLine();
   ImGui::PushStyleColor(ImGuiCol_Text, KindColour(ti.kind));
   ImGui::TextUnformatted(TrackKindName(ti.kind));
   ImGui::PopStyleColor();
   if (ti.kind == TrackKind::Absent) return;
   ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
   if (ti.revolutions > 1)
      ImGui::TextWrapped("%u cells, %d sector(s), %d weak cell(s)%s, %d revolutions", ti.cells, ti.sectors, ti.weak_cells,
                         ti.optional_cells ? " (with optional cells)" : "", ti.revolutions);
   else
      ImGui::TextWrapped("%u cells, %d sector(s), %d weak cell(s)%s", ti.cells, ti.sectors, ti.weak_cells,
                         ti.optional_cells ? " (with optional cells)" : "");
   ImGui::PopStyleColor();
   if (!ti.notes.empty()) ImGui::TextWrapped("%s", ti.notes.c_str());
   if (ti.sector_list.empty()) return;

   const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit;
   if (!ImGui::BeginTable("sectors", 7, tf)) return;
   ImGui::TableSetupScrollFreeze(0, 1);
   ImGui::TableSetupColumn("R");
   ImGui::TableSetupColumn("C/H/N");
   ImGui::TableSetupColumn("Bytes");
   ImGui::TableSetupColumn("ID");
   ImGui::TableSetupColumn("Data");
   ImGui::TableSetupColumn("Weak");
   ImGui::TableSetupColumn("Speed");
   ImGui::TableHeadersRow();
   const ImVec4 good(0.45f, 0.82f, 0.52f, 1), bad(0.95f, 0.40f, 0.40f, 1);
   for (const auto& sec : ti.sector_list)
   {
      ImGui::TableNextRow();
      ImGui::TableNextColumn(); ImGui::Text("%d", sec.r);
      ImGui::TableNextColumn(); ImGui::Text("%d/%d/%d", sec.c, sec.h, sec.n);
      ImGui::TableNextColumn(); ImGui::Text("%d", sec.size);
      ImGui::TableNextColumn(); ImGui::TextColored(sec.id_ok ? good : bad, sec.id_ok ? "ok" : "CRC");
      ImGui::TableNextColumn();
      if (sec.has_data) ImGui::TextColored(sec.data_ok ? good : bad, sec.data_ok ? "ok" : "CRC");
      else ImGui::TextDisabled("none");
      ImGui::TableNextColumn();
      if (sec.weak_cells) ImGui::Text("%d", sec.weak_cells); else ImGui::TextDisabled("-");
      ImGui::TableNextColumn();
      if (sec.density_permille >= 15 || sec.density_permille <= -15)
         ImGui::Text("%+.1f%%", sec.density_permille / 10.0);
      else
         ImGui::TextDisabled("-");
   }
   ImGui::EndTable();
}

void App::DrawLog(const std::vector<Job>& jobs)
{
   const Job* sel = nullptr;
   int done = 0, total = 0;
   for (auto& j : jobs)
   {
      if (j.id == selected_job_) sel = &j;
      if (j.status != JobStatus::Queued || converter_.Busy()) total++;
      if (j.status == JobStatus::Done || j.status == JobStatus::Failed) done++;
   }
   if (converter_.Busy())
   {
      int running_progress = 0;
      for (auto& j : jobs)
         if (j.status == JobStatus::Running) running_progress = j.progress;
      const int queued = static_cast<int>(std::count_if(jobs.begin(), jobs.end(),
                                                        [](const Job& j) { return j.status == JobStatus::Queued; }));
      const int all = done + queued + 1;
      char buf[64];
      snprintf(buf, sizeof(buf), "Disk %d of %d", done + 1, all);
      ImGui::ProgressBar((done + running_progress / 100.0f) / std::max(1, all), ImVec2(-1, 0), buf);
   }
   ImGui::Text("Log");
   if (sel)
   {
      ImGui::SameLine();
      ImGui::TextDisabled("%s%s", sel->name.c_str(),
                          sel->status == JobStatus::Done || sel->status == JobStatus::Failed
                             ? (" - " + std::to_string(static_cast<int>(sel->seconds * 10) / 10.0).substr(0, 4) + " s").c_str()
                             : "");
   }
   ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 10.0f);
   ImGui::Checkbox("Follow", &log_follow_);
   ImGui::SameLine();
   const std::string& text = (sel && sel->id == live_job_) ? live_log_ : (sel ? sel->log : live_log_);
   if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(text.c_str());
   ImGui::BeginChild("logtext", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
   ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
   ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
   ImGui::PopFont();
   if (log_follow_ && sel && sel->status == JobStatus::Running) ImGui::SetScrollHereY(1.0f);
   ImGui::EndChild();
   (void)total;
}

void App::DrawAbout()
{
   ImGui::OpenPopup("About SugarConvDsk");
   const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
   ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
   if (ImGui::BeginPopupModal("About SugarConvDsk", &show_about_, ImGuiWindowFlags_AlwaysAutoResize))
   {
      ImGui::Text("SugarConvDsk %s", SCG_VERSION);
      ImGui::TextDisabled("Flux dump converter: SCP, KryoFlux, CT-RAW, IPF, HFE -> IPF, HFE, SCP, EDSK");
      ImGui::Spacing();
      ImGui::TextUnformatted("Based on SugarConvDsk by Thomas Guillemin (Sugarbox), Atari ST support by kodak80.");
      ImGui::TextUnformatted("User interface: Dear ImGui (MIT licence).");
      ImGui::Spacing();
      if (ImGui::Button("Close", ImVec2(ImGui::GetFontSize() * 6, 0)))
      {
         show_about_ = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }
}

} // namespace scg
