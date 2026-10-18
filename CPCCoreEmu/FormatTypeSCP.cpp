#include "stdafx.h"
#include "FormatTypeSCP.h"

#include <stdlib.h>
#include <algorithm>

#ifdef _WIN32
#if _MSC_VER < 1900
extern int lround(double d);
#endif
#endif

static int AddShortToBuffer(unsigned char* buff, int index, unsigned short value)
{
   buff[index++] = ((value >> 8) & 0xFF);
   buff[index++] = (value & 0xFF);
   return index;
}

static int AddIntToBuffer(unsigned char* buff, int index, unsigned int value)
{
   buff[index++] = (value & 0xFF);
   buff[index++] = ((value >> 8) & 0xFF);
   buff[index++] = ((value >> 16) & 0xFF);
   buff[index++] = ((value >> 24) & 0xFF);
   return index;
}


FormatTypeSCP::FormatTypeSCP()
{
}

FormatTypeSCP::~FormatTypeSCP()
{
}

bool FormatTypeSCP::CanLoad(const char* file_path)
{
   // Check HXCPICFE version 
   FILE* file;
   bool can_load = false;

   if (fopen_s(&file, file_path, "rb") == 0)
   {
      // Check for type of file from header
      unsigned char header[0x3] = {0};
      // check the 22 first char
      fread(header, 1, 0x3, file);
      if (memcmp(header, "SCP", 3) == 0)
      {
         can_load = true;
      }
      fclose(file);
   }
   return can_load;
}

int FormatTypeSCP::LoadDisk(const char* file_path, IDisk*& created_disk, ILoadingProgress* loading_progress)
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

int FormatTypeSCP::LoadDisk(const unsigned char* buffer, size_t size, IDisk*& created_disk,
                            ILoadingProgress* loading_progress)
{
   unsigned char version, diskType, numRevolution;
   unsigned char startTrack, endTrack, flags;
   unsigned int check_sum_computed = 0;
   const unsigned char* header = buffer;

   if (memcmp(header, "SCP", 3) == 0)
   {
      IDisk* new_disk = new IDisk();

      version = header[3];
      diskType = header[4];
      numRevolution = header[5];
      startTrack = header[6];
      endTrack = header[7];
      flags = header[8];
      unsigned char width = header[9];
      unsigned char nb_heads = header[0x0A];
      unsigned int checkSum = *((unsigned int*)(&header[0x0C]));

      // Check the checksum !
      for (unsigned int crc = 0x10; crc < size; crc++)
      {
         check_sum_computed += header[crc];
      }
      // Compare :
      if (check_sum_computed != checkSum)
      {
         // Something to do ?
         int dbg = 1;
      }


      // Geometry.
      // The SCP track table has up to 168 entries. Byte 0x0A gives the heads that were imaged :
      // 0 = both, 1 = side 0 only, 2 = side 1 only. Most writers (SCP software for double-sided
      // dumps, Greaseweazle always) index entries as cylinder*2+head; older single-sided dumps use
      // a sequential index. Decide from the header and the table itself rather than assuming a
      // 40/42-track CPC disk, so 80-84 cylinder Atari ST dumps are split correctly.
      const int max_entries = 168;
      unsigned int first_data = static_cast<unsigned int>(size);
      int last_used = -1;
      bool any_odd = false, any_even = false;
      for (int i = 0; i < max_entries && 0x10 + i * 4 + 4 <= static_cast<int>(size); i++)
      {
         if (0x10 + i * 4 >= static_cast<int>(first_data)) break; // table can't overlap track data
         unsigned int off = *((unsigned int*)(&header[0x10 + i * 4]));
         if (off == 0 || off + 0x10 > size) continue;
         if (off < first_data) first_data = off;
         last_used = i;
         if (i & 1) any_odd = true; else any_even = true;
      }
      if (last_used < 0) return FILE_ERROR;

      bool interleaved;
      int only_head = -1;
      if (nb_heads == 0)
      {
         interleaved = true;
      }
      else
      {
         // Single head imaged : interleaved if only even (side 0) or only odd (side 1) entries are used
         interleaved = (nb_heads == 1) ? !any_odd : !any_even;
         only_head = nb_heads - 1;
      }
      const bool atari_st_type = (diskType == 0x14 || diskType == 0x15); // SCP disk types : Atari ST SS / DS
      if (nb_heads == 0 && any_odd && any_even && endTrack <= 42 && !atari_st_type)
      {
         // Legacy CPC-era single-sided dump (heads byte 0, <= 43 consecutive entries) : sequential numbering,
         // as the previous loader assumed
         interleaved = false;
         only_head = 0;
      }

      int nbtracks = interleaved ? (last_used / 2 + 1) : (last_used + 1);
      // Two sides in the internal model unless only one head was imaged
      new_disk->nb_sides_ = (interleaved && only_head == -1) ? 2 : 1;
      for (int side = 0; side < new_disk->nb_sides_; side++)
      {
         new_disk->side_[side].nb_tracks = nbtracks;
         new_disk->side_[side].tracks = new IDisk::MFMTrack[nbtracks];
         memset(new_disk->side_[side].tracks, 0, sizeof(IDisk::MFMTrack) * nbtracks);
         for (int tr = 0; tr < nbtracks; tr++)
         {
            new_disk->side_[side].tracks[tr].nb_revolutions = 1;
         }
      }

      // Track data header
      for (int i = 0; i <= last_used; i++)
      {
         tracks_header_[i].offset = *((unsigned int*)(&header[0x10 + i * 4]));

         if (tracks_header_[i].offset != 0 && tracks_header_[i].offset + 0x10 <= size)
         {
            const unsigned char* track_buffer = &header[tracks_header_[i].offset];
            // Check the TRK
            if (memcmp(&track_buffer[0], "TRK", 3) != 0)
            {
               // Error : No valid SCP file
               return -3;
            }

            // Track number (from the TRK header, which matches the table index)
            int scp_track = track_buffer[0x03];
            int side = (new_disk->nb_sides_ == 1) ? 0 : (scp_track & 0x1);
            unsigned int track = interleaved ? (scp_track >> 1) : scp_track;

            if (track >= new_disk->side_[side].nb_tracks)
            {
               continue; // Inconsistent entry : ignore rather than reject the whole dump
            }

            // Revolutions
            new_disk->side_[side].tracks[track].nb_revolutions = numRevolution;
            new_disk->side_[side].tracks[track].revolution = new IDisk::Revolution[numRevolution];

            for (int revolution = 0; revolution < numRevolution; revolution++)
            {
               //
               int duration = *((unsigned int*)(&track_buffer[0x04 + revolution * 12]));
               int length = *((unsigned int*)(&track_buffer[0x08 + revolution * 12]));
               int offset = *((unsigned int*)(&track_buffer[0x0C + revolution * 12]));

               //timeElapsed / ick * 1000;
               rpm_correction_ = (duration * 25 / 1000000) / 200.0;

               // Read the whole bitcells
               const unsigned char* cellbuffer = &track_buffer[offset];

               // Comute size in term of bits
               new_disk->side_[side].tracks[track].revolution[revolution].size = ComputeTrack(
                  NULL, cellbuffer, length, duration);

               // Fill the bitfield
               new_disk->side_[side].tracks[track].revolution[revolution].bitfield = new unsigned char[new_disk->side_[
                  side].tracks[track].revolution[revolution].size];
               IDisk::Revolution& rev = new_disk->side_[side].tracks[track].revolution[revolution];
               unsigned short* cell_ns = (revolution == 0) ? new unsigned short[rev.size] : nullptr;
               rev.size = ComputeTrack(rev.bitfield, cellbuffer, length, duration, cell_ns);
               if (cell_ns != nullptr)
               {
                  // Keep cell timing of the first revolution (Copylock ST and other density protections)
                  IDisk::MeasureSectorDensity(new_disk->side_[side].tracks[track], rev.bitfield, rev.size, cell_ns);
                  delete[] cell_ns;
               }
            }

            //AdjustLength ( m_Disk[side].Tracks[track].Revolution, numRevolution);

            if (track == 0)
            {
               //DumpTrack (0, 0, 0);
            }
         }
      }
      // Adjust missing tracks :
      for (int side = 0; side < new_disk->nb_sides_; side++)
      {
         for (int track = 0; track < static_cast<int>(new_disk->side_[side].nb_tracks); track++)
         {
            if (new_disk->side_[side].tracks[track].revolution == NULL)
            {
               // Fill this revolution with random datas
               new_disk->side_[side].tracks[track].nb_revolutions = 1;
               new_disk->side_[side].tracks[track].revolution = new IDisk::Revolution[1];
               new_disk->side_[side].tracks[track].revolution[0].bitfield = new unsigned char[6250 * 16];
               new_disk->side_[side].tracks[track].revolution[0].size = 6250 * 16;
               memset(new_disk->side_[side].tracks[track].revolution[0].bitfield, 0, 6250 * 16);
            }
         }
      }
      created_disk = new_disk;
   }
   else
   {
      // Erreur : Not a DSK file !
      return FILE_ERROR;
   }

   created_disk->CreateTrackFromMultiRevolutions();

   return OK;
}

int FormatTypeSCP::SaveDisk(const char* file_path, IDisk* disk, ILoadingProgress* loading_progress) const
{
   unsigned int checksum = 0;
   int ret = 0;
   int i;
   FILE* file;
   int offset = 0;
   unsigned char buffer[0x2A8] = {0};

   unsigned char* pTrackBuffer; // = new unsigned char[m_Disk[0].NbTracks + m_Disk[1].NbTracks];
   //int*pTrackBufferSize = new int [m_Disk[0].NbTracks + m_Disk[1].NbTracks];

   int nbSideToRecord = (disk->side_[1].nb_tracks == 0)
                           ? disk->side_[0].nb_tracks * 2
                           : disk->side_[0].nb_tracks + disk->side_[1].nb_tracks;

   int res = disk->SmartOpen(&file, file_path, ".SCP");

   if (res == 0)
   {
      // First part
      memcpy(&buffer[offset], "SCP", 3);
      offset += 3;
      buffer[offset++] = 0x11;
      buffer[offset++] = 0x00;
      buffer[offset++] = 0x01; // One revolution only
      buffer[offset++] = 0x00; // Start track
      buffer[offset++] = nbSideToRecord - 1; // End track
      buffer[offset++] = 0x00; // Flags bits
      buffer[offset++] = 0x00; // Bit cell encoding
      buffer[offset++] = 0x02; // Two side...

      offset += 1; // Reserved

      // checksum is added later
      offset = 0x2A8;
      // Tracks
      // CHEAT : If no 2nd side is recorded, just record twice the first one....


      checksum = 0;

      // Write begining...
      fwrite(buffer, 0x2A8, 1, file);

      for (unsigned char track = 0; track < nbSideToRecord; track++)
      {
         int side = (disk->side_[1].nb_tracks == 0 || disk->nb_sides_ == 1 ) ? 0 : track % 2;

         int dskTrack = /*(m_Disk[1].NbTracks==0)?track:*/track >> 1;

         int tracklength = disk->side_[side].tracks[dskTrack]/*.Revolution[0]*/.size;

         // Taille de la clock :
         // Pour un bitcell a 2us :
         // int clock = 2000 / 25; // 2 us
         // Pour un bitcell a 200ms / tracklength
         int clock = 200000000 /(25* tracklength);


         // Set the offset
         int offset_track = 0;

         // Add to header buffer
         AddIntToBuffer(buffer, 0x10 + track * sizeof(int), offset);

         // Compute max length of the buffer
         // Allocate max possible buffer
         pTrackBuffer/*[track]*/ = new unsigned char[tracklength * 2 + 0x40];
         // Max is transition every cells : 16 bits / transitions

         // Header
         memcpy(&pTrackBuffer/*[track]*/[offset_track], "TRK", 3);
         offset_track += 3;
         pTrackBuffer/*[track]*/[offset_track++] = track; // (track<<1)|track%2;

         // INDEX TIME = 32 BIT VALUE, TIME IN NANOSECONDS/25ns FOR ONE REVOLUTION
         unsigned int index_time = tracklength * clock;
         AddIntToBuffer(pTrackBuffer/*[track]*/, 0x04, 0x7A1200); // 200 ms for one revolution - TODO : TO ADJUST ?

         // Offset of revolution
         AddIntToBuffer(pTrackBuffer/*[track]*/, 0x0C, 0x10);

         offset_track = 0x10;
         // TRACK DATA = 16 BIT VALUE, TIME IN NANOSECONDS/25ns FOR ONE BIT CELL TIME
         int length = 0;
         //
         i = 0;

         // First bit : Depends on the last bit of the track
         // In fact, it it necessary to adjust to last '0' (or first '0' ) of the track, close to the index

         // If zero, dont begin with
         unsigned short flux = clock;
         unsigned int total_flux = clock;

         unsigned int index_on_track = 1;
         // Search for nearest '10'
         while (index_on_track < tracklength && disk->side_[side].tracks[dskTrack].bitfield[index_on_track-1] != 1 && disk->side_[side].tracks[dskTrack].bitfield[index_on_track ] != 0)
         {
            index_on_track++;
         }

         // HACK for "Reussir"
         /*
         if (track == 38)
         {
            index_on_track = 34950;
         }*/
         
         while (i < tracklength)
         {
            // Wait for next flux reversal
            // Handle weak bits (todo)

            while (i < tracklength && (disk->side_[side].tracks[dskTrack].bitfield[(i+index_on_track)%tracklength]&0x1) == 0x00)
            {
               // Overflow handling ?
               if ((int)(flux + clock) >= 0x10000)
               {
                  // Overflow handling TODO
               }

               flux += clock;
               total_flux += clock;
               ++i;
            }

            // Adjust bitcell to reach 200ms track
            double ratio = (double)i / (double)tracklength;
            double current = clock * (i+1);
            double total_aimed = 200000000.0 / (25.0 * (double)tracklength) * (double)(i+1.0);

            unsigned int adujst = (unsigned int)total_aimed - total_flux;
            total_flux += adujst; // NON
            flux += adujst; // NON

            // New one : Write it, and begin a new sequence
            AddShortToBuffer(pTrackBuffer/*[track]*/, offset_track, flux);
            //checksum += flux;

            offset_track += 2;
            length++;
            ++i;
            flux = clock;
            total_flux += clock;
         }
         // Last offset
         /*AddShortToBuffer ( pTrackBuffer[track], offsetTrack , flux );
         offsetTrack += 2;
         length++;*/

         //
         //pTrackBufferSize[track] = offsetTrack;
         // Track length
         offset += offset_track;
         AddIntToBuffer(pTrackBuffer/*[track]*/, 0x08, length); // 200 ms for one revolution - TODO : TO ADJUST ?

         for (int crc = 0; crc < offset_track; crc++)
         {
            checksum += pTrackBuffer[crc];
         }

         fwrite(pTrackBuffer, offset_track, 1, file);
         delete[]pTrackBuffer;
      }

      // Timestamp


      // --- Checksum : Compute it
      // Header
      for (i = 0x10; i < 0x2A8; i++) checksum += buffer[i];
      // Write it
      // Timestamp
      // TODO

      // Write it
      memcpy(&buffer[0x0C], (unsigned int*)&checksum, 4);
      /*buffer[0x0C] =  (checksum & 0xFF);
      buffer[0x0D] =  ((checksum>>1) & 0xFF);
      buffer[0x0E] =  ((checksum>>2) & 0xFF);
      buffer[0x0F] =  ((checksum>>3) & 0xFF);*/

      // --- Write buffers
      // Go back to begining of file
      fseek(file, 0, SEEK_SET);
      fwrite(buffer, 0x2A8, 1, file);

      /*// Track buffers
      for (int track = 0; track < m_Disk[0].NbTracks + m_Disk[1].NbTracks; track ++)
      {
      fwrite ( pTrackBuffer[track],  pTrackBufferSize[track], 1, file) ;
      }

      */

      disk->disk_modified_ = false;
      fclose(file);
   }

   //delete []pTrackBufferSize;

   /*for (unsigned char track = 0; track < m_Disk[0].NbTracks + m_Disk[1].NbTracks; track ++)
   delete (pTrackBuffer[track]);
   delete []pTrackBuffer;*/

   return 0;
}

unsigned int FormatTypeSCP::ComputeTrack(unsigned char* bitfield, const unsigned char* cellbuffer, int sizeOfBuffer,
                                         int duration, unsigned short* cell_ns)
{
#define CLOCK_MAX_ADJ 10     /* +/- 10% adjustment */
#define CLOCK_MIN(_c) (((_c) * (100 - CLOCK_MAX_ADJ)) / 100)
#define CLOCK_MAX(_c) (((_c) * (100 + CLOCK_MAX_ADJ)) / 100)
#define PERIOD_ADJ_PCT 5

   int flux = 0;
   int clock = 2000; // 2 us
   int clock_centre = clock;
   int i = 0;
   unsigned int nb_bits_in_flux = 0;
   int clocked_zeros = 0;
   int localDuration;

   while (i + 1 < sizeOfBuffer * 2 || (flux >= clock / 2))
   {
      // What's the current flux status
      // Under a half clock : add the next one
      if (flux < clock / 2)
      {
         int nb_bytes = 0;
         int time = 0;

         while (time == 0 && i + 1 < sizeOfBuffer * 2)
         {
            localDuration = ((cellbuffer[i] << 8) | cellbuffer[i + 1]);
            time = localDuration * 25;
            if (time == 0)
            {
               i += 2;
               ++nb_bytes;
            }
         }

         // Each zero entry is a 16-bit counter overflow : 65536 ticks of 25 ns
         time += nb_bytes * 0x10000 * 25;
         i += 2;
         flux += time;
         clocked_zeros = 0;
      }

      // Remove a clock value to the flux
      flux -= clock;

      // If it remains at least a half clock : A zero's there
      if (flux >= clock / 2)
      {
         if (bitfield != NULL) bitfield[nb_bits_in_flux] = 0;
         if (cell_ns != NULL) cell_ns[nb_bits_in_flux] = static_cast<unsigned short>(clock);
         clocked_zeros++;
         nb_bits_in_flux++;
      }
      else
      {
         // Otherwise, we have a one
         if (i + 1 < sizeOfBuffer * 2)
         {
            if (bitfield != NULL) bitfield[nb_bits_in_flux] = 1;
            if (cell_ns != NULL) cell_ns[nb_bits_in_flux] = static_cast<unsigned short>(clock);
            nb_bits_in_flux++;
         }

         // PLL computations HERE !
         if ((clocked_zeros >= 1) && (clocked_zeros <= 3))
         {
            /* In sync: adjust base clock by 10% of phase mismatch. */
            int diff = flux / (int)(clocked_zeros + 1);
            clock += diff / PERIOD_ADJ_PCT;
         }
         else
         {
            /* Out of sync: adjust base clock towards centre. */
            clock += (clock_centre - clock) * PERIOD_ADJ_PCT / 100;
         }

         /* Clamp the clock's adjustment range. */
         clock = std::max(CLOCK_MIN(clock_centre),
                          std::min(CLOCK_MAX(clock_centre), clock));
         flux = flux / 2;
      }
   }

   return nb_bits_in_flux;
}

int FormatTypeSCP::LoadDisk(std::vector<TrackItem> file_list, IDisk*& created_disk, ILoadingProgress* loading_progress)
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
