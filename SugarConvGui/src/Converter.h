#pragma once
//////////////////////////////////////////////////////////
// SugarConvGui - conversion engine
//
// Plain C++ interface to the SugarConvDsk core (CPCCoreEmu). This header must not include any core
// header: the core's stdafx.h redefines Windows types, so only Converter.cpp / DiskAnalysis.cpp
// see the core, and the Win32 front end only sees this file.
//////////////////////////////////////////////////////////

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace scg
{

enum class OutFormat { IPF = 0, HFE, SCP, EDSK, Count };
const char* OutFormatName(OutFormat f);        // "IPF"...
const char* OutFormatDescription(OutFormat f); // "IPF (SPS / CAPS)"...
const char* OutFormatExtension(OutFormat f);   // ".IPF"...

struct Options
{
   OutFormat format = OutFormat::IPF;
   int platform = 1;          // 0 = guess from the dump, 1 = Atari ST, 2 = Amstrad CPC
   int sides = 0;             // 0 = both, 1 = side 1 only, 2 = side 2 only
   bool speedlock10 = false;  // Speedlock key as IPF density type 6 (+10% / -10%) instead of 7
   bool ipf_raw = false;      // old IPF encoding (MFM cells instead of decoded bytes)
   bool scp_noflux = false;   // SCP: weak areas as no-flux gaps (for writing to a real disk)
   bool hfe_v1 = false;       // HFE: original v1 format instead of v3
   bool output_next_to_source = true;
   std::string output_dir;    // used when output_next_to_source is false
   bool recursive = true;     // folder scan
   std::string filter;        // folder scan wildcard, e.g. "*.scp" (empty = all supported)
};

// ---------------------------------------------------------------- disk summary (track map)
enum class TrackKind : uint8_t
{
   Absent = 0,     // not in the dump
   Unformatted,    // no sectors, no data stream
   Ok,             // sectors, all CRCs good
   CrcError,       // a sector with a bad ID or data CRC (and no weak bits to explain it)
   Weak,           // weak / fuzzy bits
   PhaseSlip,      // weak + optional cells (Copylock short tracks, Time disk 2)
   DensityKey,     // Copylock / Speedlock speed changes
   Stream,         // custom data-stream track (MacroDOS)
   Count
};
const char* TrackKindName(TrackKind k);

struct SectorInfo
{
   int c = 0, h = 0, r = 0, n = 0;
   int size = 0;
   bool id_ok = false;
   bool data_ok = false;
   bool has_data = false;
   int weak_cells = 0;
   int density_permille = 0; // cell length deviation from the track median (+50 = 5% slower)
};

struct TrackInfo
{
   TrackKind kind = TrackKind::Absent;
   unsigned int cells = 0;
   int sectors = 0;
   int bad_sectors = 0;
   int weak_cells = 0;
   int optional_cells = 0;
   int revolutions = 0;
   bool stream = false;
   std::string notes;        // e.g. "Speedlock key in R5", "long track"
   std::vector<SectorInfo> sector_list;
};

constexpr int kMaxTracks = 90;

struct DiskSummary
{
   bool valid = false;
   std::string platform;     // "Atari ST" / "Amstrad CPC"
   std::string source_format;
   int sides = 0;
   int tracks = 0;           // highest track count of both sides
   TrackInfo track[2][kMaxTracks];
   std::string protections;  // one-line summary
};

// ---------------------------------------------------------------- jobs
enum class JobStatus { Queued, Running, Done, Failed };

struct Job
{
   int id = 0;
   std::string source;       // file to load (first file of a KryoFlux set)
   std::string name;         // display / output base name
   std::string output;       // file written (after the conversion)
   JobStatus status = JobStatus::Queued;
   bool analyse_only = false;
   int progress = 0;         // 0..100 while loading
   std::string message;      // short result
   std::string log;          // converter output
   double seconds = 0;
   std::shared_ptr<DiskSummary> summary;
};

// Files the converter can read, from a list of paths (files and folders). Folders are scanned
// (options.recursive, options.filter); a KryoFlux stream set counts once (its track 00.0 file).
std::vector<std::string> CollectSources(const std::vector<std::string>& paths, const Options& options);
// Base name used for the output of a source (KryoFlux "track00.0.raw" -> its folder name)
std::string OutputBaseName(const std::string& source);
// Full output path without the extension (name : the job's name, "" = from the source)
std::string OutputPathFor(const std::string& source, const Options& options, const std::string& name = std::string());
bool IsSupportedFile(const std::string& path);

class Converter
{
public:
   Converter();
   ~Converter();

   // Queue management (thread safe)
   int Add(const std::string& source);               // returns the job id
   void Remove(int id);
   void ClearFinished();
   void ClearAll();
   std::vector<Job> Snapshot();                      // copy of the queue for drawing
   std::shared_ptr<DiskSummary> Summary(int id);

   // Run the given jobs (empty = every queued job) with these options
   void Start(const Options& options, const std::vector<int>& ids, bool analyse_only);
   void Requeue(const std::vector<int>& ids);
   void StopAfterCurrent();
   bool Busy() const { return busy_; }
   int CurrentJob() const { return current_; }

   // Text written by the converter since the last call (the running job's log)
   std::string TakeLiveLog();

private:
   void WorkerLoop();
   void RunJob(int id, const Options& options, bool analyse_only);

   std::mutex mutex_;
   std::condition_variable cv_;
   std::vector<Job> jobs_;
   std::deque<int> pending_;
   Options run_options_;
   bool run_analyse_ = false;
   int next_id_ = 1;
   std::atomic<bool> busy_{false};
   std::atomic<bool> stop_after_current_{false};
   std::atomic<int> current_{0};
   bool quit_ = false;
   std::thread worker_;

   std::string log_path_;
   std::mutex live_mutex_;
   long long live_offset_ = 0;
   bool live_active_ = false;
};

} // namespace scg
