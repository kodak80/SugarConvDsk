#include "stdafx.h"
#include "FormatTypeHFE.h"
#include <vector>
#include <algorithm>


#define IBMPC_DD_FLOPPYMODE 0x00
#define IBMPC_HD_FLOPPYMODE 0x01
#define ATARIST_DD_FLOPPYMODE 0x02
#define ATARIST_HD_FLOPPYMODE 0x03
#define AMIGA_DD_FLOPPYMODE 0x04
#define AMIGA_HD_FLOPPYMODE 0x05
#define CPC_DD_FLOPPYMODE 0x06
#define GENERIC_SHUGGART_DD_FLOPPYMODE 0x07
#define IBMPC_ED_FLOPPYMODE 0x08
#define MSX2_DD_FLOPPYMODE 0x09
#define C64_DD_FLOPPYMODE 0x0A
#define EMU_SHUGART_FLOPPYMODE 0x0B
#define S950_DD_FLOPPYMODE 0x0C
#define S950_HD_FLOPPYMODE 0x0D
#define DISABLE_FLOPPYMODE 0xFE

#define ISOIBM_MFM_ENCODING 0x00
#define AMIGA_MFM_ENCODING 0x01
#define ISOIBM_FM_ENCODING 0x02
#define EMU_FM_ENCODING 0x03
#define UNKNOWN_ENCODING 0xFF


typedef struct Picfileformatheader
{
   unsigned char header_signature[8]; // “HXCPICFE”
   unsigned char formatrevision; // Revision 0
   unsigned char number_of_track; // Number of track in the file
   unsigned char number_of_side; // Number of valid side (Not used by the emulator)
   unsigned char track_encoding; // Track Encoding mode
   // (Used for the write support - Please see the list above)
   unsigned short bit_rate; // Bitrate in Kbit/s. Ex : 250=250000bits/s
   // Max value : 500
   unsigned short floppy_rpm; // Rotation per minute (Not used by the emulator)
   unsigned char floppyinterfacemode; // Floppy interface mode. (Please see the list above.)
   unsigned char dnu; // Free
   unsigned short track_list_offset; // Offset of the track list LUT in block of 512 bytes
   // (Ex: 1=0x200)
   unsigned char write_allowed; // The Floppy image is write protected ?
   unsigned char single_step; // 0xFF : Single Step – 0x00 Double Step mode
   unsigned char track0_s0_altencoding; // 0x00 : Use an alternate track_encoding for track 0 Side 0
   unsigned char track0_s0_encoding; // alternate track_encoding for track 0 Side 0
   unsigned char track0_s1_altencoding; // 0x00 : Use an alternate track_encoding for track 0 Side 1
   unsigned char track0_s1_encoding; // alternate track_encoding for track 0 Side 1
} Picfileformatheader;

typedef struct 
{
   unsigned short offset; // Offset of the track data in block of 512 bytes (Ex: 2=0x400)
   unsigned short track_len; // Length of the track data in byte.
} Pictrack;


// HFE v3 opcodes, as stored in the file (HFE bytes are LSB-first, so these are the bit-reversed
// 0xF0..0xF4 of the HxC documentation). A data byte never starts with 4 one-cells in valid MFM.
#define HFE3_OP_NOP      0x0F
#define HFE3_OP_INDEX    0x8F
#define HFE3_OP_BITRATE  0x4F   // + 1 byte : cell period in 36 MHz / 2 ticks (72 = 250 kbit/s)
#define HFE3_OP_SKIPBITS 0xCF   // + 1 byte : skip 1-7 cells of the following byte
#define HFE3_OP_RAND     0x2F   // a byte of random cells (weak / fuzzy bits)
#define HFE3_NOMINAL_RATE 72

bool FormatTypeHFE::write_v1_ = false;

static unsigned char ReverseBits(unsigned char v)
{
   unsigned char r = 0;
   for (int i = 0; i < 8; i++) if (v & (1 << i)) r |= 0x80 >> i;
   return r;
}

FormatTypeHFE::FormatTypeHFE()
{
}

FormatTypeHFE::~FormatTypeHFE()
{
}

const char* FormatTypeHFE::GetFormatName()
{
   return "HFE";
}

const char* FormatTypeHFE::GetFormatDescriptor()
{
   return "HFE";
}

const char* FormatTypeHFE::GetFormatExt()
{
   return "hfe";
}

bool FormatTypeHFE::CanLoad() const
{
   return true;
}

bool FormatTypeHFE::CanSave() const
{
   return true;
}

bool FormatTypeHFE::CanLoad(const char* file_path)
{
   // Check HXCPICFE version 
   FILE* file;
   bool can_load = false;

   if (fopen_s(&file, file_path, "rb") == 0)
   {
      // Check for type of file from header
      unsigned char header[0x8] = {0};
      // check the 22 first char
      fread(header, 1, 0x8, file);
      if (memcmp(header, "HXCPICFE", 8) == 0 || memcmp(header, "HXCHFEV3", 8) == 0)
      {
         can_load = true;
      }
      fclose(file);
   }
   return can_load;
}

int FormatTypeHFE::LoadDisk(const char* file_path, IDisk*& created_disk, ILoadingProgress* loading_progress)
{
   int return_value = -1;
   FILE* file;

   if (fopen_s(&file, file_path, "rb") == 0)
   {
      // Read whole file
      fseek(file, 0, SEEK_END);
      unsigned int size = ftell(file);
      rewind(file);
      unsigned char* buffer = new unsigned char[size];
      if (fread(buffer, 1, size, file) == size)
      {
         return_value = LoadDisk(buffer, size, created_disk, loading_progress);
      }
      else
      {
         return_value = FILE_ERROR;
      }
      delete[]buffer;

      fclose(file);
   }
   else
   {
      // Erreur : File not found
      return_value = FILE_ERROR;
   }
   return return_value;
}

int FormatTypeHFE::LoadDisk(const unsigned char* buffer, size_t size, IDisk*& created_disk,
                            ILoadingProgress* loading_progress)
{
   const bool v3 = (size >= 8 && memcmp(buffer, "HXCHFEV3", 8) == 0);
   if (size >= 0x400 && (v3 || memcmp(buffer, "HXCPICFE", 8) == 0))
   {
      IDisk* new_disk = new IDisk();

      // Header
      int index_buffer = 0;

      Picfileformatheader header;
      //fread(&header, 1, sizeof(header), file);
      memcpy(&header, buffer, sizeof(header));
      index_buffer += sizeof(header);

      // Number of sides
      new_disk->nb_sides_ = header.number_of_side;

      for (int side = 0; side < new_disk->nb_sides_; side++)
      {
         new_disk->side_[side].nb_tracks = header.number_of_track;
         new_disk->side_[side].tracks = new IDisk::MFMTrack[new_disk->side_[side].nb_tracks];
         memset(new_disk->side_[side].tracks, 0, sizeof(IDisk::MFMTrack) * new_disk->side_[side].nb_tracks);
      }

      //fseek(file, 0x200, SEEK_SET);
      index_buffer = 0x200;

      Pictrack* pck_trk = new Pictrack[header.number_of_track];
      //fread(pck_trk, 1, sizeof(pck_trk) * header.number_of_track, file);
      memcpy(pck_trk, &buffer[index_buffer], sizeof(Pictrack) * header.number_of_track);
      index_buffer += sizeof(pck_trk) * header.number_of_track;

      // Second part : (up to 1024 bytes) : Track offset LUT
      unsigned char* buffer1;
      unsigned char* buffer2;
      for (int track = 0; track < header.number_of_track; track++)
      {
         // Track data
         //fseek(file, 0x200 * pck_trk[track].offset, SEEK_SET);
         index_buffer = 0x200 * pck_trk[track].offset;

         // Read bytes
         buffer1 = new unsigned char[pck_trk[track].track_len / 2];
         buffer2 = new unsigned char[pck_trk[track].track_len / 2];

         int byte_to_read = pck_trk[track].track_len / 2;
         unsigned char buf[0x200];
         int index = 0;
         while (byte_to_read > 0)
         {
            //fread(buf, 1, 0x200, file);
            memset(buf, 0, sizeof(buf));
            if (index_buffer < size)
               memcpy(buf, &buffer[index_buffer], std::min<size_t>(0x200, size - index_buffer));
            index_buffer += 0x200;

            unsigned int cnt = (byte_to_read < 0x100) ? byte_to_read : 0x100;

            memcpy(&buffer1[index], buf, cnt);
            memcpy(&buffer2[index], &buf[0x100], cnt);

            byte_to_read -= cnt;
            index += cnt;
         }


         for (int side = 0; side < new_disk->nb_sides_; side++)
         {
            const unsigned char* src = (side == 0) ? buffer1 : buffer2;
            const unsigned int nb_bytes = pck_trk[track].track_len / 2;
            std::vector<unsigned char> cells;
            cells.reserve(nb_bytes * 8);
            for (unsigned int i = 0; i < nb_bytes; i++)
            {
               unsigned char x = src[i];
               unsigned int skip = 0;
               unsigned char flag = 0;
               if (v3 && (x & 0x0F) == 0x0F)
               {
                  if (x == HFE3_OP_BITRATE) { i++; continue; }     // timing isn't kept in the bit model
                  if (x == HFE3_OP_SKIPBITS && i + 2 < nb_bytes)
                  {
                     skip = ReverseBits(src[i + 1]) & 7;
                     i += 2;
                     x = src[i];
                     if (x == HFE3_OP_RAND) { x = 0; flag = BIT_WEAK; }
                  }
                  else if (x == HFE3_OP_RAND) { x = 0; flag = BIT_WEAK; }
                  else continue;                                     // NOP, INDEX
               }
               for (unsigned int b = skip; b < 8; b++) cells.push_back(((x >> b) & 1) | flag);
            }
            IDisk::MFMTrack& t = new_disk->side_[side].tracks[track];
            t.size = static_cast<unsigned int>(cells.size());
            t.bitfield = new unsigned char[t.size + 1];
            if (t.size) memcpy(t.bitfield, cells.data(), t.size);
         }
         delete[]buffer1;
         delete[]buffer2;
      }

      delete[]pck_trk;

      created_disk = new_disk;
      return OK;
   }
   else
   {
      return FILE_ERROR;
   }
}

// One side of one track, encoded as HFE bytes (cells LSB-first).
static std::vector<unsigned char> EncodeTrackSide(IDisk* disk, int side, unsigned int track, bool v3,
                                                  unsigned int& fixed_bytes)
{
   std::vector<unsigned char> out;
   if (side >= disk->nb_sides_ || track >= disk->side_[side].nb_tracks) return out;
   const IDisk::MFMTrack& t = disk->side_[side].tracks[track];
   const unsigned char* bits = t.bitfield;
   unsigned int n = t.size;
   if (bits == nullptr || n == 0) return out;

   // A track the merge couldn't build (unformatted / noise) is a 16 KB weak placeholder. Write one
   // revolution of it (100000 cells at 300 rpm), so HFE track lengths stay realistic and in range.
   if (n > 110000)
   {
      std::vector<std::pair<unsigned int, unsigned int>> fields;
      IDisk::FindDataFields(bits, n, fields);
      if (fields.empty()) n = 100000;
   }

   // Cell rate per range (v3 only) : parts of the track measured faster/slower than the rest are
   // written with their own bit rate. A whole slow sector (Copylock ST) is timed from its ID field;
   // a patch inside a sector (Speedlock-style density key) only over the patch itself.
   std::vector<std::pair<unsigned int, unsigned char>> rate_changes; // cell position -> rate
   if (v3 && (t.nb_sector_density > 0 || t.nb_density_runs > 0))
   {
      std::vector<std::pair<unsigned int, unsigned int>> fields;
      IDisk::FindDataFields(bits, n, fields);
      auto rate_of = [](int permille)
      {
         return static_cast<unsigned char>(HFE3_NOMINAL_RATE * (1000 + permille) / 1000.0 + 0.5);
      };
      for (size_t k = 0; k < fields.size(); k++)
      {
         const unsigned int field_bytes = (fields[k].second - fields[k].first) / 16;
         bool partial = false;
         for (int r = 0; r < t.nb_density_runs; r++)
         {
            const IDisk::MFMTrack::DensityRun& run = t.density_runs[r];
            if (run.field != k || run.byte_len * 10 >= field_bytes * 8) continue;
            partial = true;
            const unsigned int start = fields[k].first + (run.byte_start + 1u) * 16u;
            const unsigned int end = start + run.byte_len * 16u;
            if (end > n) continue;
            rate_changes.emplace_back(start & ~7u, rate_of(run.permille));
            rate_changes.emplace_back((end + 7) & ~7u, HFE3_NOMINAL_RATE);
         }
         if (partial || k >= t.nb_sector_density) continue;
         const int d = t.sector_density[k];
         if (d < 25 && d > -25) continue;
         // The protection times the whole sector : start at its ID field (~48 bytes before the data)
         unsigned int start = (fields[k].first > 48 * 16) ? fields[k].first - 48 * 16 : 0;
         rate_changes.emplace_back(start & ~7u, rate_of(d));
         rate_changes.emplace_back((fields[k].second + 7) & ~7u, HFE3_NOMINAL_RATE);
      }
      std::sort(rate_changes.begin(), rate_changes.end(),
                [](const std::pair<unsigned int, unsigned char>& x, const std::pair<unsigned int, unsigned char>& y)
                { return x.first < y.first; });
   }

   if (v3)
   {
      out.push_back(HFE3_OP_INDEX);
      out.push_back(HFE3_OP_BITRATE);
      out.push_back(ReverseBits(HFE3_NOMINAL_RATE));
   }

   size_t next_rate = 0;
   for (unsigned int c = 0; c < n; c += 8)
   {
      while (v3 && next_rate < rate_changes.size() && rate_changes[next_rate].first <= c)
      {
         out.push_back(HFE3_OP_BITRATE);
         out.push_back(ReverseBits(rate_changes[next_rate].second));
         next_rate++;
      }
      unsigned int cnt = (n - c < 8) ? n - c : 8;
      unsigned char b = 0;
      bool weak = false;
      for (unsigned int i = 0; i < cnt; i++)
      {
         unsigned char cell = bits[c + i];
         if (cell & (BIT_WEAK | BIT_OPTIONAL)) weak = true;
         if (cell & 1) b |= 1 << i;
      }
      if (cnt < 8)
      {
         // Last partial byte : the remaining cells are put at the end of a byte, the start is skipped
         if (v3)
         {
            out.push_back(HFE3_OP_SKIPBITS);
            out.push_back(ReverseBits(static_cast<unsigned char>(8 - cnt)));
            out.push_back(weak ? HFE3_OP_RAND : static_cast<unsigned char>(b << (8 - cnt)));
         }
         else
         {
            out.push_back(b);
         }
         break;
      }
      if (v3 && weak)
      {
         out.push_back(HFE3_OP_RAND);
         continue;
      }
      if (v3 && (b & 0x0F) == 0x0F)
      {
         // Four one-cells in a row can't be MFM (only unformatted noise) and would read as an opcode
         b &= ~0x08;
         fixed_bytes++;
      }
      out.push_back(b);
   }
   return out;
}

int FormatTypeHFE::SaveDisk(const char* file_path, IDisk* disk, ILoadingProgress* loading_progress) const
{
   int ret = -1;
   FILE* file;
   int res = disk->SmartOpen(&file, file_path, ".HFE");
   if (res != 0) return ret;

   const bool v3 = !write_v1_;
   const bool atari_st = (disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST);
   unsigned int nb_tracks = disk->side_[0].nb_tracks;
   if (disk->nb_sides_ > 1 && disk->side_[1].nb_tracks > nb_tracks) nb_tracks = disk->side_[1].nb_tracks;
   if (nb_tracks > 255) nb_tracks = 255;

   // First part : Header
   unsigned char buffer[0x200];
   memset(buffer, 0xFF, sizeof(buffer));

   Picfileformatheader header;
   memset(&header, 0xFF, sizeof(header));
   memcpy(header.header_signature, v3 ? "HXCHFEV3" : "HXCPICFE", sizeof(header.header_signature));
   header.formatrevision = 0;
   header.number_of_track = static_cast<unsigned char>(nb_tracks);
   header.number_of_side = (disk->nb_sides_ > 1) ? 2 : 1;
   header.track_encoding = ISOIBM_MFM_ENCODING;
   header.bit_rate = 250;
   header.floppy_rpm = 300;
   header.floppyinterfacemode = atari_st ? ATARIST_DD_FLOPPYMODE : CPC_DD_FLOPPYMODE;
   header.dnu = 1;
   header.track_list_offset = 1; // Offset of the track list = 512
   header.write_allowed = 0xFF;
   header.single_step = 0xFF;
   header.track0_s0_altencoding = 0xFF;
   header.track0_s0_encoding = 0xFF;
   header.track0_s1_altencoding = 0xFF;
   header.track0_s1_encoding = 0xFF;

   memcpy(buffer, &header, sizeof(header));
   fwrite(buffer, 1, sizeof(buffer), file);

   // Second part : Track offset LUT (written once the offsets are known)
   std::vector<Pictrack> lut(nb_tracks);
   const unsigned int lut_blocks = (nb_tracks * sizeof(Pictrack) + 0x1FF) / 0x200;
   memset(buffer, 0xFF, sizeof(buffer));
   for (unsigned int i = 0; i < lut_blocks; i++) fwrite(buffer, 1, 0x200, file);

   // Third part : Track data, 256 bytes of side 0 then 256 bytes of side 1 per 512 byte block
   unsigned int fixed_bytes = 0;
   for (unsigned int track = 0; track < nb_tracks; track++)
   {
      std::vector<unsigned char> sides[2];
      for (int s = 0; s < 2; s++) sides[s] = EncodeTrackSide(disk, s, track, v3, fixed_bytes);
      size_t len = std::max(sides[0].size(), sides[1].size());
      // Pad the shorter side (or an absent one) : NOP in v3, gap-free filler in v1
      for (int s = 0; s < 2; s++) sides[s].resize(len, v3 ? HFE3_OP_NOP : 0x00);
      if (len * 2 > 0xFFFF)
      {
         printf("(track %u too long for HFE, truncated) ", track);
         len = 0xFFFF / 2;
      }

      lut[track].offset = static_cast<unsigned short>(ftell(file) / 0x200);
      lut[track].track_len = static_cast<unsigned short>(len * 2);
      for (size_t pos = 0; pos < len; pos += 0x100)
      {
         memset(buffer, v3 ? HFE3_OP_NOP : 0x00, sizeof(buffer));
         size_t cnt = std::min<size_t>(0x100, len - pos);
         memcpy(buffer, &sides[0][pos], cnt);
         memcpy(&buffer[0x100], &sides[1][pos], cnt);
         fwrite(buffer, 1, 0x200, file);
      }
   }

   // Write now the LUT
   fseek(file, 0x200, SEEK_SET);
   fwrite(lut.data(), sizeof(Pictrack), lut.size(), file);
   fclose(file);
   if (fixed_bytes != 0)
   {
      printf("(%u non-MFM byte(s) adjusted for HFE v3) ", fixed_bytes);
   }
   ret = 0;
   return ret;
}

int FormatTypeHFE::LoadDisk(std::vector<TrackItem> file_list, IDisk*& created_disk, ILoadingProgress* loading_progress)
{
   if (file_list.size() == 1)
   {
      return LoadDisk(file_list[0].buffer, file_list[0].size, created_disk, loading_progress);
   }
   else
   {
      return NOT_IMPLEMENTED;
   }
}
