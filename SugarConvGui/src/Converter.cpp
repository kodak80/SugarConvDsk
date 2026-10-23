//////////////////////////////////////////////////////////
// SugarConvGui - conversion engine (the only GUI file that sees the core)
//////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DiskBuilder.h"
#include "FormatTypeHFE.h"
#include "FormatTypeSCP.h"
#include "ILoadingProgress.h"
#include "IDisk.h"
// CAPSFile.h ends with #pragma pack(4) and never restores it : include it last (only its static
// options are used here) so the classes above keep the layout the core was compiled with, and
// restore the packing for the std containers below.
#pragma pack(push)
#include "CAPSFile.h"
#pragma pack(pop)

#include "Converter.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#ifdef _WIN32
#include <io.h>
#define SCG_NULL_DEVICE "NUL"
#else
#include <unistd.h>
#define SCG_NULL_DEVICE "/dev/null"
#endif

namespace stdfs = std::filesystem;

namespace scg
{

// ------------------------------------------------------------------------------------------------
// Names

const char* OutFormatName(OutFormat f)
{
   switch (f)
   {
   case OutFormat::IPF: return "IPF";
   case OutFormat::HFE: return "HFE";
   case OutFormat::SCP: return "SCP";
   case OutFormat::EDSK: return "EDSK";
   default: return "?";
   }
}

const char* OutFormatDescription(OutFormat f)
{
   switch (f)
   {
   case OutFormat::IPF: return "SPS / CAPS, for emulators";
   case OutFormat::HFE: return "Gotek / HxC (v3 keeps weak bits and speed keys)";
   case OutFormat::SCP: return "SuperCard Pro flux, emulators or writing back";
   case OutFormat::EDSK: return "Extended DSK, Amstrad CPC";
   default: return "?";
   }
}

const char* OutFormatExtension(OutFormat f)
{
   switch (f)
   {
   case OutFormat::IPF: return ".IPF";
   case OutFormat::HFE: return ".hfe";
   case OutFormat::SCP: return ".scp";
   case OutFormat::EDSK: return ".dsk";
   default: return "";
   }
}

const char* TrackKindName(TrackKind k)
{
   switch (k)
   {
   case TrackKind::Absent: return "Not in dump";
   case TrackKind::Unformatted: return "Unformatted";
   case TrackKind::Ok: return "OK";
   case TrackKind::CrcError: return "CRC error";
   case TrackKind::Weak: return "Weak bits";
   case TrackKind::PhaseSlip: return "Phase slip";
   case TrackKind::DensityKey: return "Speed key";
   case TrackKind::Stream: return "Data stream";
   default: return "?";
   }
}

// ------------------------------------------------------------------------------------------------
// Sources

static std::string Lower(std::string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return s;
}

static bool WildcardMatch(const std::string& text, const std::string& pattern)
{
   // '*' and '?' wildcards, case insensitive
   const std::string t = Lower(text), p = Lower(pattern);
   size_t ti = 0, pi = 0, star = std::string::npos, mark = 0;
   while (ti < t.size())
   {
      if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) { ti++; pi++; }
      else if (pi < p.size() && p[pi] == '*') { star = pi++; mark = ti; }
      else if (star != std::string::npos) { pi = star + 1; ti = ++mark; }
      else return false;
   }
   while (pi < p.size() && p[pi] == '*') pi++;
   return pi == p.size();
}

// KryoFlux stream file : <prefix>NN.S.raw
static bool KryoFluxParts(const std::string& filename, std::string& prefix, int& track, int& side)
{
   static const std::regex re("^(.*?)(\\d{2,3})\\.([01])\\.raw$", std::regex::icase);
   std::smatch m;
   if (!std::regex_match(filename, m, re)) return false;
   prefix = m[1].str();
   track = std::stoi(m[2].str());
   side = std::stoi(m[3].str());
   return true;
}

bool IsSupportedFile(const std::string& path)
{
   const std::string ext = Lower(stdfs::path(path).extension().string());
   static const std::set<std::string> exts = {".scp", ".raw", ".ctr", ".ipf", ".hfe", ".dsk", ".edsk"};
   return exts.count(ext) != 0;
}

std::string OutputBaseName(const std::string& source)
{
   const stdfs::path p(source);
   std::string prefix;
   int track, side;
   if (KryoFluxParts(p.filename().string(), prefix, track, side))
   {
      while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == '-' || prefix.back() == ' ' || prefix.back() == '.'))
         prefix.pop_back();
      if (prefix.empty() || Lower(prefix) == "track")
      {
         const std::string folder = p.parent_path().filename().string();
         return folder.empty() ? std::string("disk") : folder;
      }
      return prefix;
   }
   return p.stem().string();
}

std::string OutputPathFor(const std::string& source, const Options& options, const std::string& name)
{
   const stdfs::path src(source);
   stdfs::path dir = (options.output_next_to_source || options.output_dir.empty()) ? src.parent_path()
                                                                                    : stdfs::path(options.output_dir);
   return (dir / (name.empty() ? OutputBaseName(source) : name)).string();
}

std::vector<std::string> CollectSources(const std::vector<std::string>& paths, const Options& options)
{
   std::vector<std::string> out;
   std::set<std::string> seen;
   // KryoFlux sets : one entry per folder + prefix, the lowest track / side file
   std::map<std::string, std::pair<int, std::string>> kf_sets;

   auto consider = [&](const stdfs::path& file, bool from_folder)
   {
      const std::string name = file.filename().string();
      if (!IsSupportedFile(name)) return;
      if (from_folder)
      {
         if (!options.filter.empty())
         {
            if (!WildcardMatch(name, options.filter)) return;
         }
         else
         {
            // a folder scan picks up flux dumps only (not IPF / HFE / DSK images, often earlier outputs)
            const std::string ext = Lower(file.extension().string());
            if (ext != ".scp" && ext != ".raw" && ext != ".ctr") return;
         }
      }
      std::string prefix;
      int track, side;
      if (KryoFluxParts(name, prefix, track, side))
      {
         const std::string key = (file.parent_path() / prefix).string();
         const int order = track * 2 + side;
         auto it = kf_sets.find(key);
         if (it == kf_sets.end() || order < it->second.first) kf_sets[key] = {order, file.string()};
         return;
      }
      const std::string s = file.string();
      if (seen.insert(s).second) out.push_back(s);
   };

   for (const auto& p : paths)
   {
      std::error_code ec;
      const stdfs::path path(p);
      if (stdfs::is_directory(path, ec))
      {
         if (options.recursive)
         {
            for (auto it = stdfs::recursive_directory_iterator(path, stdfs::directory_options::skip_permission_denied, ec);
                 it != stdfs::recursive_directory_iterator(); it.increment(ec))
            {
               if (ec) break;
               if (it->is_regular_file(ec)) consider(it->path(), true);
            }
         }
         else
         {
            for (auto& e : stdfs::directory_iterator(path, ec))
               if (e.is_regular_file(ec)) consider(e.path(), true);
         }
      }
      else if (stdfs::is_regular_file(path, ec))
      {
         consider(path, false);
      }
   }
   for (auto& kv : kf_sets)
      if (seen.insert(kv.second.second).second) out.push_back(kv.second.second);
   std::sort(out.begin(), out.end());
   return out;
}

// ------------------------------------------------------------------------------------------------
// Disk analysis for the track map

static int CountFlag(const unsigned char* bits, unsigned int size, unsigned int from, unsigned int to, unsigned char flag)
{
   if (bits == nullptr || size == 0) return 0;
   int n = 0;
   if (to < from) to += size; // field running across the index
   for (unsigned int i = from; i < to; i++)
      if (bits[i % size] & flag) n++;
   return n;
}

static void AnalyseDisk(IDisk* disk, DiskSummary& s)
{
   s.valid = true;
   s.platform = disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST ? "Atari ST" : "Amstrad CPC";
   s.sides = disk->GetNumberOfSide();
   s.tracks = std::min<int>(kMaxTracks, static_cast<int>(disk->GetNumberOfTracks()));
   int n_weak = 0, n_stream = 0, n_key = 0, n_copylock = 0, n_slip = 0, n_crc = 0, n_long = 0;

   for (int side = 0; side < 2 && side < s.sides; side++)
   {
      const int nb = std::min<int>(kMaxTracks, static_cast<int>(disk->side_[side].nb_tracks));
      for (int tr = 0; tr < nb; tr++)
      {
         IDisk::MFMTrack& t = disk->side_[side].tracks[tr];
         TrackInfo& ti = s.track[side][tr];
         ti.cells = t.size;
         ti.revolutions = static_cast<int>(t.nb_revolutions);
         if (t.bitfield == nullptr || t.size == 0)
         {
            ti.kind = TrackKind::Absent;
            continue;
         }
         ti.weak_cells = CountFlag(t.bitfield, t.size, 0, t.size, BIT_WEAK);
         ti.optional_cells = CountFlag(t.bitfield, t.size, 0, t.size, BIT_OPTIONAL);

         IDisk::Track info;
         disk->GetTrackInfo(side, tr, &info);
         // data fields in physical order, for the per-sector timing
         std::vector<const IDisk::Sector*> by_data;
         for (auto& sec : info.list_sector_)
            if (sec.dam_offset != 0) by_data.push_back(&sec);
         std::sort(by_data.begin(), by_data.end(),
                   [](const IDisk::Sector* a, const IDisk::Sector* b) { return a->dam_offset < b->dam_offset; });

         bool slow_sector = false;
         for (auto& sec : info.list_sector_)
         {
            SectorInfo si;
            si.c = sec.c; si.h = sec.h; si.r = sec.r; si.n = sec.n;
            si.size = sec.real_size;
            si.id_ok = (sec.status1 & 0x20) == 0;
            si.has_data = sec.dam_offset != 0;
            si.data_ok = si.has_data && (sec.status2 & 0x20) == 0;
            if (si.has_data)
            {
               si.weak_cells = CountFlag(t.bitfield, t.size, sec.dam_offset, sec.index_end, BIT_WEAK);
               for (size_t k = 0; k < by_data.size(); k++)
                  if (by_data[k] == &sec && k < t.nb_sector_density) si.density_permille = t.sector_density[k];
               if (si.density_permille >= 30) slow_sector = true;
            }
            if (!si.id_ok || (si.has_data && !si.data_ok)) ti.bad_sectors++;
            ti.sector_list.push_back(si);
         }
         ti.sectors = static_cast<int>(ti.sector_list.size());

         unsigned int nb_syncs = 0;
         if (ti.sectors == 0)
            ti.stream = IDisk::FindStreamSync(t.bitfield, t.size, nb_syncs) >= 0;

         // speed key : a slow and a fast run inside the same data field
         bool speedlock = false;
         for (int i = 0; i < t.nb_density_runs && !speedlock; i++)
            for (int j = 0; j < t.nb_density_runs; j++)
               if (t.density_runs[i].field == t.density_runs[j].field && t.density_runs[i].permille > 0 &&
                   t.density_runs[j].permille < 0)
                  speedlock = true;

         std::string notes;
         auto add_note = [&](const std::string& n) { notes += (notes.empty() ? "" : ", ") + n; };
         if (ti.sectors == 0 && !ti.stream)
         {
            ti.kind = TrackKind::Unformatted;
         }
         else if (ti.stream)
         {
            ti.kind = TrackKind::Stream;
            add_note("custom data stream (no sectors)");
            n_stream++;
         }
         else if (speedlock)
         {
            ti.kind = TrackKind::DensityKey;
            add_note("Speedlock-style key (slow then fast patch)");
            n_key++;
         }
         else if (slow_sector)
         {
            ti.kind = TrackKind::DensityKey;
            add_note("Copylock-style slow sector");
            n_copylock++;
         }
         else if (ti.weak_cells > 0 && ti.optional_cells > 0)
         {
            ti.kind = TrackKind::PhaseSlip;
            add_note("phase-slip cells");
            n_slip++;
         }
         else if (ti.weak_cells > 0)
         {
            ti.kind = TrackKind::Weak;
            add_note("weak / fuzzy bits");
            n_weak++;
         }
         else if (ti.bad_sectors > 0)
         {
            ti.kind = TrackKind::CrcError;
            n_crc++;
         }
         else
         {
            ti.kind = TrackKind::Ok;
         }
         if (ti.kind != TrackKind::Unformatted)
         {
            if (ti.bad_sectors > 0) add_note(std::to_string(ti.bad_sectors) + " sector(s) with CRC errors");
            if (ti.sectors > 11) add_note(std::to_string(ti.sectors) + " sectors");
            if (t.size > 102000) { add_note("long track"); n_long++; }
         }
         ti.notes = notes;
      }
   }

   std::string p;
   auto add = [&](int n, const char* what) { if (n) p += (p.empty() ? "" : ", ") + std::to_string(n) + " " + what; };
   add(n_copylock, "Copylock-style");
   add(n_key, "Speedlock-style");
   add(n_stream, "data-stream");
   add(n_weak, "weak-bit");
   add(n_slip, "phase-slip");
   add(n_long, "long");
   add(n_crc, "CRC-error");
   s.protections = p.empty() ? std::string("no protection tracks found") : p + " track(s)";
}

// ------------------------------------------------------------------------------------------------
// Converter

namespace
{
class Progress : public ILoadingProgress
{
public:
   explicit Progress(std::vector<Job>& jobs, std::mutex& m, int id) : jobs_(jobs), m_(m), id_(id) {}
   void SetProgress(int progress) override
   {
      std::lock_guard<std::mutex> lock(m_);
      for (auto& j : jobs_)
         if (j.id == id_) j.progress = std::max(0, std::min(100, progress));
   }
private:
   std::vector<Job>& jobs_;
   std::mutex& m_;
   int id_;
};
}

Converter::Converter()
{
   std::error_code ec;
   stdfs::path tmp = stdfs::temp_directory_path(ec);
   if (ec) tmp = ".";
   log_path_ = (tmp / "SugarConvGui_log.txt").string();
   worker_ = std::thread([this] { WorkerLoop(); });
}

Converter::~Converter()
{
   {
      std::lock_guard<std::mutex> lock(mutex_);
      quit_ = true;
      pending_.clear();
   }
   cv_.notify_all();
   if (worker_.joinable()) worker_.join();
}

int Converter::Add(const std::string& source)
{
   std::lock_guard<std::mutex> lock(mutex_);
   for (auto& j : jobs_)
      if (j.source == source) return j.id; // already queued
   Job j;
   j.id = next_id_++;
   j.source = source;
   j.name = OutputBaseName(source);
   // two dumps of the same disk (e.g. a KryoFlux set and a CT-RAW) would write the same file
   auto taken = [this](const std::string& n)
   {
      for (auto& o : jobs_)
         if (Lower(o.name) == Lower(n)) return true;
      return false;
   };
   if (taken(j.name))
   {
      std::string prefix, ext = Lower(stdfs::path(source).extension().string());
      int track, side;
      const std::string tag = KryoFluxParts(stdfs::path(source).filename().string(), prefix, track, side)
                                 ? std::string("KF") : (ext.size() > 1 ? ext.substr(1) : std::string("2"));
      std::string n = j.name + " (" + tag + ")";
      for (int k = 2; taken(n); k++) n = j.name + " (" + tag + " " + std::to_string(k) + ")";
      j.name = n;
   }
   jobs_.push_back(j);
   return j.id;
}

void Converter::Remove(int id)
{
   std::lock_guard<std::mutex> lock(mutex_);
   if (id == current_) return;
   jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [id](const Job& j) { return j.id == id; }), jobs_.end());
   pending_.erase(std::remove(pending_.begin(), pending_.end(), id), pending_.end());
}

void Converter::ClearFinished()
{
   std::lock_guard<std::mutex> lock(mutex_);
   jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const Job& j)
                              { return j.status == JobStatus::Done || j.status == JobStatus::Failed; }),
               jobs_.end());
}

void Converter::ClearAll()
{
   std::lock_guard<std::mutex> lock(mutex_);
   pending_.clear();
   const int cur = current_;
   jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [cur](const Job& j) { return j.id != cur; }), jobs_.end());
}

std::vector<Job> Converter::Snapshot()
{
   std::lock_guard<std::mutex> lock(mutex_);
   return jobs_;
}

std::shared_ptr<DiskSummary> Converter::Summary(int id)
{
   std::lock_guard<std::mutex> lock(mutex_);
   for (auto& j : jobs_)
      if (j.id == id) return j.summary;
   return nullptr;
}

void Converter::Start(const Options& options, const std::vector<int>& ids, bool analyse_only)
{
   {
      std::lock_guard<std::mutex> lock(mutex_);
      if (busy_) return;
      run_options_ = options;
      run_analyse_ = analyse_only;
      pending_.clear();
      for (auto& j : jobs_)
      {
         const bool wanted = ids.empty() ? (j.status == JobStatus::Queued)
                                         : (std::find(ids.begin(), ids.end(), j.id) != ids.end());
         if (!wanted) continue;
         j.status = JobStatus::Queued;
         j.progress = 0;
         j.message.clear();
         pending_.push_back(j.id);
      }
      if (pending_.empty()) return;
      stop_after_current_ = false;
      busy_ = true;
   }
   cv_.notify_all();
}

void Converter::Requeue(const std::vector<int>& ids)
{
   std::lock_guard<std::mutex> lock(mutex_);
   for (auto& j : jobs_)
      if (std::find(ids.begin(), ids.end(), j.id) != ids.end() && j.id != current_)
      {
         j.status = JobStatus::Queued;
         j.progress = 0;
         j.message.clear();
      }
}

void Converter::StopAfterCurrent()
{
   stop_after_current_ = true;
}

std::string Converter::TakeLiveLog()
{
   std::lock_guard<std::mutex> lock(live_mutex_);
   if (!live_active_) return std::string();
   std::ifstream f(log_path_, std::ios::binary);
   if (!f) return std::string();
   f.seekg(0, std::ios::end);
   const long long size = static_cast<long long>(f.tellg());
   if (size <= live_offset_) return std::string();
   f.seekg(live_offset_);
   std::string text(static_cast<size_t>(size - live_offset_), '\0');
   f.read(&text[0], static_cast<std::streamsize>(text.size()));
   live_offset_ = size;
   return text;
}

void Converter::WorkerLoop()
{
   for (;;)
   {
      int id = 0;
      Options options;
      bool analyse = false;
      {
         std::unique_lock<std::mutex> lock(mutex_);
         cv_.wait(lock, [this] { return quit_ || !pending_.empty(); });
         if (quit_) return;
         if (stop_after_current_)
         {
            for (int pid : pending_)
               for (auto& j : jobs_)
                  if (j.id == pid) j.message = "stopped";
            pending_.clear();
            busy_ = false;
            continue;
         }
         id = pending_.front();
         pending_.pop_front();
         options = run_options_;
         analyse = run_analyse_;
         current_ = id;
         for (auto& j : jobs_)
            if (j.id == id) j.status = JobStatus::Running;
      }
      RunJob(id, options, analyse);
      {
         std::lock_guard<std::mutex> lock(mutex_);
         current_ = 0;
         if (pending_.empty()) busy_ = false;
      }
   }
}

void Converter::RunJob(int id, const Options& options, bool analyse_only)
{
   std::string source, name;
   {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& j : jobs_)
         if (j.id == id) { source = j.source; name = j.name; }
   }
   const auto t0 = std::chrono::steady_clock::now();

   // The core reports to stdout : send it to a file the UI can follow
   fflush(stdout);
   {
      std::lock_guard<std::mutex> lock(live_mutex_);
      FILE* f = freopen(log_path_.c_str(), "w", stdout);
      (void)f;
      setvbuf(stdout, nullptr, _IOFBF, 4096); // the core prints a lot ; the UI shows it in chunks
      live_offset_ = 0;
      live_active_ = true;
   }

   // Options are static members of the core classes
   CAPSFile::compact_ = !options.ipf_raw;
   CAPSFile::speedlock_density_ = options.speedlock10 ? 6 : 7;
   FormatTypeSCP::weak_as_noflux_ = options.scp_noflux;
   FormatTypeHFE::write_v1_ = options.hfe_v1;

   bool ok = false;
   std::string message, output;
   auto summary = std::make_shared<DiskSummary>();
   {
      DiskBuilder builder;
      Progress progress(jobs_, mutex_, id);
      FormatType* in_format = nullptr;
      IDisk* disk = nullptr;
      printf("Loading %s\n", source.c_str());
      if (!builder.CanLoad(source.c_str(), in_format))
      {
         message = "unknown format";
      }
      else if (builder.LoadDisk(source.c_str(), disk, &progress) != 0 || disk == nullptr)
      {
         message = "could not load the dump";
      }
      else
      {
         summary->source_format = in_format ? in_format->GetFormatName() : "";
         const IDisk::FaceSelection want = options.sides == 1 ? IDisk::FACE_1
                                         : options.sides == 2 ? IDisk::FACE_2 : IDisk::FACE_BOTH;
         if (disk->FilterSide(want) == IDisk::NO_FACE)
         {
            message = "side not present";
         }
         else
         {
            disk->SetPlatform(options.platform == 1 ? IDisk::PLATFORM_ATARI_ST
                              : options.platform == 2 ? IDisk::PLATFORM_CPC : IDisk::PLATFORM_AUTO);
            printf("\n%s - %s\n", summary->source_format.c_str(),
                   disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST ? "Atari ST" : "Amstrad CPC");
            AnalyseDisk(disk, *summary);
            printf("Tracks: %s\n", summary->protections.c_str());
            if (analyse_only)
            {
               ok = true;
               message = "analysed";
            }
            else
            {
               FormatType* out_format = nullptr;
               for (auto* f : builder.GetFormatsList(DiskBuilder::WRITE))
                  if (strcmp(f->GetFormatName(), OutFormatName(options.format)) == 0) out_format = f;
               const std::string dest = OutputPathFor(source, options, name);
               std::error_code ec;
               stdfs::create_directories(stdfs::path(dest).parent_path(), ec);
               if (out_format == nullptr)
               {
                  message = "output format not available";
               }
               else if (out_format->SaveDisk(dest.c_str(), disk) == FormatType::OK)
               {
                  ok = true;
                  output = disk->GetCurrentLoadedDisk();
                  if (output.empty()) output = dest + OutFormatExtension(options.format);
                  printf("\nSaved %s\n", output.c_str());
                  message = "saved";
               }
               else
               {
                  message = "could not save";
               }
            }
         }
         delete disk;
      }
   }

   fflush(stdout);
   std::string log;
   {
      std::lock_guard<std::mutex> lock(live_mutex_);
      FILE* f = freopen(SCG_NULL_DEVICE, "w", stdout);
      (void)f;
      live_active_ = false;
      std::ifstream in(log_path_, std::ios::binary);
      log.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
   }

   const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
   std::lock_guard<std::mutex> lock(mutex_);
   for (auto& j : jobs_)
   {
      if (j.id != id) continue;
      j.status = ok ? JobStatus::Done : JobStatus::Failed;
      j.progress = 100;
      j.message = message;
      j.output = output;
      j.log = log;
      j.seconds = secs;
      j.analyse_only = analyse_only;
      if (summary->valid) j.summary = summary;
   }
}

} // namespace scg
