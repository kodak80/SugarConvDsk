#include "stdafx.h"
#include "CAPSFile.h"
#include <algorithm>
#include <vector>

#include "CRC.h"
#include "FDC765.h"


unsigned int SwapInt(unsigned int dword)
{
   unsigned int tmp = (dword >> 24)
      + (((dword >> 16) & 0xFF) << 8)
      + (((dword >> 8) & 0xFF) << 16)
      + (((dword) & 0xFF) << 24);
   return tmp;
}

int Swap(unsigned char* buffer, unsigned int size)
{
   // Swap every unsigned dword in this buffer
   unsigned char buf[4];
   for (unsigned int i = 0; i < size; i += 4)
   {
      memcpy(buf, &buffer[i], 4);
      buffer[i] = buf[3];
      buffer[i + 1] = buf[2];
      buffer[i + 2] = buf[1];
      buffer[i + 3] = buf[0];
   }
   return 0;
}

CAPSFile::CAPSFile(): encoder_type_(0)
{
}


CAPSFile::~CAPSFile()
{
   CAPSFile::Clear();
}

void CAPSFile::Clear()
{
   for (std::vector<DataChunks>::iterator it = list_data_.begin(); it != list_data_.end(); it++)
   {
      delete []it->buffer;
      it->buffer = NULL;
   }
   list_data_.clear();
   list_imge_.clear();
   list_tracks_.clear();
}

unsigned short ExtractShort(const unsigned char* in)
{
   unsigned short return_value;
#ifdef BIG_ENDIAN
   return_value = in[1]
      + (in[0] << 8);
#else
   return_value = in[0]
      + (in[1] << 8);

#endif

   return return_value;
}

unsigned int ExtractInt(const unsigned char* in)
{
   unsigned int return_value;
#ifdef BIG_ENDIAN
   return_value = in[3]
      + (in[2] << 8)
      + (in[1] << 16)
      + (in[0] << 24);
#else
   return_value = in[0]
      + (in[1] << 8)
      + (in[2] << 16)
      + (in[3] << 24);

#endif

   return return_value;
}


bool CAPSFile::ReadRecordHeader(const unsigned char* buffer, RecordHeader* header_out, size_t size)
{
   memcpy(header_out->type, &buffer[0], 4);
   header_out->length = ExtractInt(&buffer[4]);
   header_out->crc = ExtractInt(&buffer[8]);

   // Copy buffer to check
   if (header_out->length > size) return false;

   unsigned char* buffer_to_check = new unsigned char[header_out->length];
   memcpy(buffer_to_check, buffer, header_out->length);
   // Check the CRC
   memset(&buffer_to_check[8], 0, 4);

   unsigned int crc = CRC::ComputeCrc32(0xEDB88320, buffer_to_check, header_out->length);
   delete[] buffer_to_check;

   return (crc == header_out->crc);
}

bool CAPSFile::GetStreamGapItem(CAPSFile::StreamItem* item, unsigned char* buffer, unsigned int& offset,
                                unsigned int size_of_buffer)
{
   // Next stream item
   unsigned char b = buffer[offset++];

   if (b == 0) return false;

   memset(item, 0, sizeof(StreamItem));
   while (b != 0 && offset < size_of_buffer)
   {
      int size = b >> 5;
      int type = b & 0x1F;
      int gap_size = 0;
      for (int j = 0; j < size; j++)
      {
         gap_size += (buffer[offset++] << (8 * (size - (j + 1))));
      }

      if (type == 1)
      {
         item->size = gap_size;
      }
      else if (type == 2)
      {
         item->sample = buffer[offset++];
         item->sample_size = gap_size;
      }
      else return false;

      b = buffer[offset++];
   }
   return true;
}

int CAPSFile::ReadBuffer(const unsigned char* buffer, unsigned int size_of_buffer, IDisk* disk,
                         ILoadingProgress* loading_progress)
{
   // While there is something to read
   RecordHeader rec_header;
   const unsigned char* block;
   int block_size;
   unsigned int gen_index = 0;

   list_imge_.clear();

   while (gen_index < size_of_buffer)
   {
      if (!ReadRecordHeader(&buffer[gen_index], &rec_header, (size_of_buffer - gen_index)))
      {
         // CRC ERROR !
         Clear();
         return -1;
      }
      gen_index += 12;

      // Block size
      block_size = rec_header.length - 12;;
      block = &buffer[gen_index];
      gen_index += block_size;

      // Next record header
      if (memcmp(rec_header.type, "CAPS", 4) == 0)
      {
         // Nothing interesting here
      }
      else if (memcmp(rec_header.type, "DUMP", 4) == 0)
      {
      }
      else if (memcmp(rec_header.type, "PACK", 4) == 0)
      {
         int offset = 0;
         unsigned int csize = ExtractInt(&block[offset]);; // compressed size in bytes
         offset += 4;
         unsigned int ccrc = ExtractInt(&block[offset]);; // CRC on compressed data
         offset += 4;
         unsigned int hcrc = ExtractInt(&block[offset]);; // CRC on header calculated as hcrc=0
         offset += 4;

         // Read compressed size ?
         if (csize + 8 != rec_header.length)
         {
            Clear();
            return -1;
         }
      }
      else if (memcmp(rec_header.type, "TRCK", 4) == 0)
      {
         //
         Trackchunk item;
         int offset = 0;
         item.ctype = ExtractInt(&block[offset]);; // type of track
         offset += 4;
         item.cyl = ExtractInt(&block[offset]);; // cylinder
         offset += 4;
         item.head = ExtractInt(&block[offset]);; // head
         offset += 4;
         item.did = ExtractInt(&block[offset]);; // id
         offset += 4;

         list_tracks_.push_back(item);
      }
      else if (memcmp(rec_header.type, "DATA", 4) == 0)
      {
         int offset = 0;
         DataChunks data_item;

         // Length
         data_item.size = ExtractInt(&block[offset]);
         offset += 4;
         // Bitsize
         data_item.bit_size = ExtractInt(&block[offset]);
         offset += 4;
         // CRC
         data_item.data_crc = ExtractInt(&block[offset]);
         offset += 4;
         // datakey
         data_item.data_id = ExtractInt(&block[offset]);
         offset += 4;

         data_item.buffer = new unsigned char[data_item.size];
         memcpy(data_item.buffer, &block[offset], data_item.size);

         gen_index += data_item.size;
         // Save it
         list_data_.push_back(data_item);

         if (list_imge_.size() > 0)
         {
            unsigned int crc = CRC::ComputeCrc32(0xEDB88320, &block[offset], data_item.size);
            if (data_item.data_crc != crc)
            {
               // CRC ERROR : TODO
               Clear();

               return -1;
            }


            DecodeData(data_item, disk);
         }
      }
      else if (memcmp(rec_header.type, "INFO", 4) == 0)
      {
         unsigned int side_min, side_max;
         unsigned int track_min, track_max;
         // check size !
         if (block_size != 84) return -1;

         int offset = 0;
         // Decode Info header :
         // Media type
         ExtractInt(&block[offset]);
         offset += 4;
         // Encoder type
         encoder_type_ = ExtractInt(&block[offset]);
         offset += 4;
         // Encoder rev
         ExtractInt(&block[offset]);
         offset += 4;
         // File key
         ExtractInt(&block[offset]);
         offset += 4;
         // File rev
         ExtractInt(&block[offset]);
         offset += 4;
         // origin CRC32
         ExtractInt(&block[offset]);
         offset += 4;
         // Min track
         track_min = ExtractInt(&block[offset]);
         offset += 4;
         // Max track
         track_max = ExtractInt(&block[offset]);
         offset += 4;
         // Min side
         side_min = ExtractInt(&block[offset]);
         offset += 4;
         // Max side
         side_max = ExtractInt(&block[offset]);
         offset += 4;
         // Creation date
         ExtractInt(&block[offset]);
         offset += 4;
         // Creation time
         ExtractInt(&block[offset]);
         offset += 4;
         // Platform
         ExtractInt(&block[offset]);
         offset += 4;
         ExtractInt(&block[offset]);
         offset += 4;
         ExtractInt(&block[offset]);
         offset += 4;
         ExtractInt(&block[offset]);
         offset += 4;

         // Disk number
         ExtractInt(&block[offset]);
         offset += 4;
         // creator ID
         ExtractInt(&block[offset]);
         offset += 4;
         // reserved -
         offset += 12;

         disk->nb_sides_ = side_max - side_min + 1;
         for (int side = 0; side < disk->nb_sides_; side++)
         {
            disk->side_[side].nb_tracks = track_max - track_min + 1;
            disk->side_[side].tracks = new IDisk::MFMTrack[disk->side_[side].nb_tracks];
            for (unsigned int tr = 0; tr < disk->side_[side].nb_tracks; tr++)
            {
               disk->side_[side].tracks[tr].size = 0;
               disk->side_[side].tracks[tr].bitfield = nullptr;
               disk->side_[side].tracks[tr].nb_revolutions = 0;
               disk->side_[side].tracks[tr].revolution = nullptr;
            }
         }
      }
      else if (memcmp(rec_header.type, "IMGE", 4) == 0)
      {
         int offset = 0;
         ImgeItem item;

         // Track number
         item.track_number = ExtractInt(&block[offset]);
         offset += 4;
         item.side = ExtractInt(&block[offset]);
         offset += 4;

         if (item.side >= disk->nb_sides_ || item.track_number >= static_cast<int>(disk->side_[item.side].nb_tracks))
         {
            Clear();
            return -1;
         }

         // density : Not used for amstrad
         item.density = ExtractInt(&block[offset]);
         offset += 4;
         // signal Type : Should be 2us cell (1)
         item.signal_type = ExtractInt(&block[offset]);
         offset += 4;
         // trackBytes
         item.track_byte = ExtractInt(&block[offset]);
         offset += 4;

         // startbytepos : useless
         offset += 4;
         // startbitpos
         item.start_bit_pos = ExtractInt(&block[offset]);
         offset += 4;
         // Databits
         item.data_bits = ExtractInt(&block[offset]);
         offset += 4;
         // gapbits
         item.gap_bits = ExtractInt(&block[offset]);
         offset += 4;
         //trackbits
         item.track_bits = ExtractInt(&block[offset]);
         offset += 4;
         // blockcount
         item.block_count = ExtractInt(&block[offset]);
         offset += 4;
         // encoderprocess
         item.encoder_process = ExtractInt(&block[offset]);
         offset += 4;
         // trackflags
         item.track_flags = ExtractInt(&block[offset]);
         offset += 4;
         // data keys
         item.data_key = ExtractInt(&block[offset]);
         offset += 4;
         // reserved
         offset += 12;

         disk->side_[item.side].tracks[item.track_number].nb_revolutions = 0;
         disk->side_[item.side].tracks[item.track_number].revolution = 0;

         disk->side_[item.side].tracks[item.track_number].bitfield = new unsigned char[item.track_bits];
         disk->side_[item.side].tracks[item.track_number].size = item.track_bits;
         memset(disk->side_[item.side].tracks[item.track_number].bitfield, 0xFF,
                disk->side_[item.side].tracks[item.track_number].size);

         // Add IMGE bloc
         list_imge_.push_back(item);
      }
      else
      {
         // Not handled
         Clear();
         return -1;
      }
   }

   // CT-RAW ?
   if (list_tracks_.size() > 0)
   {
      // Count number of tracks and heads
      unsigned int max_side = 0;
      unsigned int max_track = 0;
      for (std::vector<Trackchunk>::iterator trackit = list_tracks_.begin(); trackit != list_tracks_.end(); trackit++)
      {
         if (trackit->cyl > max_track)
            max_track = trackit->cyl;
         if (trackit->head > max_side)
            max_side = trackit->head;
      }

      disk->nb_sides_ = max_side + 1;
      for (int side = 0; side < disk->nb_sides_; side++)
      {
         disk->side_[side].nb_tracks = max_track + 1;
         disk->side_[side].tracks = new IDisk::MFMTrack[disk->side_[side].nb_tracks];

         for (unsigned int tr = 0; tr < disk->side_[side].nb_tracks; tr++)
         {
            disk->side_[side].tracks[tr].nb_revolutions = 1;
            disk->side_[side].tracks[tr].revolution = new IDisk::Revolution[1];

            // Each track : 9 sector
            disk->side_[side].tracks[tr].revolution[0].size = 6250 * 16;
            disk->side_[side].tracks[tr].revolution[0].bitfield = new unsigned char[disk->side_[side].tracks[tr].
               revolution[0].size];
            memset(disk->side_[side].tracks[tr].revolution[0].bitfield, BIT_WEAK,
                   disk->side_[side].tracks[tr].revolution[0].size);
         }
      }

      int total = 0;
      for (std::vector<Trackchunk>::iterator trackit = list_tracks_.begin(); trackit != list_tracks_.end(); trackit++)
      {
         // Decompress data !
         // Search associated datas to decode
         bool found = false;
         DataChunks image_item;
         for (std::vector<DataChunks>::iterator it = list_data_.begin(); it != list_data_.end() && (!found); it++)
         {
            if (it->data_id == trackit->did)
            {
               // Found !
               image_item = *it;
               found = true;
            }
         }
         if (found)
         {
            // Do something !
            DecompressData(disk, trackit->ctype, trackit->cyl, trackit->head, image_item);
            disk->CreateSingleTrackFromMultiRevolutions(trackit->head, trackit->cyl);

            IDisk::Track track_info;
            disk->GetTrackInfo(trackit->head, trackit->cyl, &track_info);
         }
         if (loading_progress != nullptr)
         {
            total++;
            loading_progress->SetProgress(total * 100 / list_tracks_.size());
         }
      }
   }

   // Clear side (if side 2 is empty, remove it)
   if (disk->nb_sides_ == 2)
   {
      bool do_not_remove = false;
      for (unsigned int tr = 0; tr < disk->side_[1].nb_tracks && (!do_not_remove); tr++)
      {
         // CTR ?
         if ((disk->side_[1].tracks[tr].revolution != NULL && disk->side_[1].tracks[tr].revolution[0].size != 0)
            || (disk->side_[1].tracks[tr].revolution == NULL && disk->side_[1].tracks[tr].size != 0)
         )
         {
            do_not_remove = true;
         }
      }

      if (!do_not_remove)
      {
         for (unsigned int tr = 0; tr < disk->side_[1].nb_tracks; tr++)
         {
            if (disk->side_[1].tracks[tr].revolution != NULL)
               delete[]disk->side_[1].tracks[tr].revolution;
         }
         delete[]disk->side_[1].tracks;
         disk->nb_sides_ = 1;
      }
   }

   // Clear all
   Clear();

   return 0;
}

bool CAPSFile::DecodeData(DataChunks item, IDisk* disk)
{
   unsigned int offset = 0;
   unsigned char* block = item.buffer;
   unsigned char* data_block = &block[offset];

   int total_total = 0;
   // Now, extra data blocks ?
   if (item.size != 0)
   {
      // Get block count from IMGE
      ImgeItem imge_item;
      bool found = false;
      for (std::vector<ImgeItem>::iterator it = list_imge_.begin(); it != list_imge_.end() && (!found); it++)
      {
         if (it->data_key == item.data_id)
         {
            // Found !
            imge_item = *it;
            found = true;
         }
      }
      if (!found || (imge_item.block_count * 32 > item.size))
      {
         // Error !
         return false;
         // TODO : Save this chunk for later use ?
         //break;
      }
      else
      {
         int total_data = 0;

         unsigned int index_track = imge_item.start_bit_pos;
         std::vector<std::pair<unsigned int, unsigned int>> block_spans; // data bits, gap bits
         for (DWORD i = 0; i < imge_item.block_count; i++)
         {
            // Read descriptors
            int data_bits = ExtractInt(&block[offset]);
            offset += 4;
            unsigned int gap_bits = ExtractInt(&block[offset]);
            offset += 4;
            block_spans.emplace_back(static_cast<unsigned int>(data_bits), gap_bits);
            int data_bytes = 0;
            int gap_bytes = 0;
            unsigned int gap_offset = 0;
            int cell_type = 0;
            if (encoder_type_ == 1)
            {
               data_bytes = ExtractInt(&block[offset]);
               offset += 4;
               gap_bytes = ExtractInt(&block[offset]);
               offset += 4;
            }
            else
            {
               gap_offset = ExtractInt(&block[offset]);
               offset += 4;
               cell_type = ExtractInt(&block[offset]);
               offset += 4;
            }
            int encoder_type = ExtractInt(&block[offset]);
            offset += 4;
            int block_flags = ExtractInt(&block[offset]);
            offset += 4;
            int gap_default = ExtractInt(&block[offset]);
            offset += 4;
            int data_offset = ExtractInt(&block[offset]);
            offset += 4;

            // Now, remaining datas
            unsigned char* current_block_gap = &data_block[gap_offset];
            unsigned char* current_block_data = &data_block[data_offset];
            data_offset = 0;
            gap_offset = 0;
            // Do we have backward gap ?
            // Read Forward / Backward streams
            StreamItem backward_stream = {0};
            StreamItem forward_stream = {0};

            unsigned int total_gap = 0;
            bool forward_gap = false;
            bool backward_gap = false;
            if ((block_flags & 0x1) == 0x1)
            {
               // Ok, Forward stream
               forward_gap = GetStreamGapItem(&forward_stream, current_block_gap, gap_offset, item.size);
            }
            if ((block_flags & 0x2) == 0x2)
            {
               // Ok, backward stream
               backward_gap = GetStreamGapItem(&backward_stream, current_block_gap, gap_offset, item.size);
            }

            // data
            unsigned char b = current_block_data[data_offset++];
            unsigned int offset_before = index_track;
            while (b != 0)
            {
               int size_width = b >> 5;
               int data_type = b & 0x1F;
               int data_size = 0;
               for (int j = 0; j < size_width; j++)
               {
                  data_size += (current_block_data[data_offset++] << (8 * (size_width - (j + 1))));
               }
               if ((block_flags & 0x4) == 0x4)
               {
                  // in bits
                  while (data_size != 0)
                  {
                     if (data_type == 5)
                     {
                        for (int c = 0; c < data_size; c++)
                        {
                           disk->side_[imge_item.side].tracks[imge_item.track_number].bitfield[index_track++] =
                              BIT_WEAK;
                           if (index_track >= disk->side_[imge_item.side].tracks[imge_item.track_number].size)
                              index_track = 0;
                        }
                        data_size = 0;
                     }
                     else
                     {
                        unsigned char bitfield = current_block_data[data_offset++];
                        if (data_type == 1)
                        {
                           if (data_size > 8)
                           {
                              int maxbits = 8;
                              for (int d = 0; d < 8; d++)
                              {
                                 maxbits--;
                                 int val = (bitfield >> maxbits) & 0x1;
                                 disk->side_[imge_item.side].tracks[imge_item.track_number].bitfield[index_track++] =
                                    val;
                                 if (index_track >= disk->side_[imge_item.side].tracks[imge_item.track_number].size)
                                    index_track = 0;
                              }
                              data_size -= 8;
                           }
                           else
                           {
                              int maxbits = 8;
                              for (int d = 0; d < data_size; d++)
                              {
                                 maxbits--;
                                 int val = (bitfield >> maxbits) & 0x1;
                                 disk->side_[imge_item.side].tracks[imge_item.track_number].bitfield[index_track++] =
                                    val;
                                 if (index_track >= disk->side_[imge_item.side].tracks[imge_item.track_number].size)
                                    index_track = 0;
                              }
                              data_size = 0;
                           }
                        }
                           // in bytes
                        else
                        {
                           // Convert to MFM
                           if (data_size >= 8)
                           {
                              index_track = disk->AddByteToSpecificTrack(
                                 imge_item.side, imge_item.track_number, index_track, bitfield);
                              data_size -= 8;
                           }
                           else
                           {
                              // Convert to MFM
                              index_track = disk->AddByteToSpecificTrack(
                                 imge_item.side, imge_item.track_number, index_track, bitfield, data_size * 2);
                              data_size -= data_size;
                           }
                        }
                     }
                  }
               }
               else
               {
                  // in bytes
                  if (data_type == 5)
                  {
                     // weak bit ??
                     unsigned char byte = 0xE5;

                     for (int c = 0; c < data_size * 16; c++)
                     {
                        if (c > 256 * 16 || data_size != 512)
                        {
                           disk->side_[imge_item.side].tracks[imge_item.track_number].bitfield[index_track++] =
                              BIT_WEAK;
                           if (index_track >= disk->side_[imge_item.side].tracks[imge_item.track_number].size)
                              index_track = 0;
                        }
                        else
                        {
                           index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number,
                                                                      index_track, byte);
                           c += 15;
                        }
                     }
                     //dataoffset++;
                  }
                  else
                  {
                     if (data_type == 3)
                     {
                        //totalGap += dataSize*16;
                     }
                     for (int c = 0; c < data_size; c++)
                     {
                        unsigned char byte = 0;
                        {
                           byte = current_block_data[data_offset++];

                           if (data_type == 1)
                           {
                              index_track = disk->AddMFMByteToAllTrack(imge_item.side, imge_item.track_number,
                                                                       index_track, byte, 8);
                           }
                           else
                           {
                              index_track = disk->AddByteToSpecificTrack(
                                 imge_item.side, imge_item.track_number, index_track, byte);
                           }
                        }
                     }
                  }
               }
               b = current_block_data[data_offset++];
            }
            if (offset_before > index_track)
            {
               offset_before = offset_before - disk->side_[imge_item.side].tracks[imge_item.track_number].size;
            }
            total_total += data_bits; //indexTrack - offsetBefore;
            total_data += data_bits;
            if ((data_bits != index_track - offset_before)
               && ((index_track != offset_before)
                  || (data_bits != disk->side_[imge_item.side].tracks[imge_item.track_number].size)
               )
            )
            {
               //
               int dbg = 1;
            }


            if (encoder_type_ == 1)
            {
               // GAP via CAPS encoding
               for (unsigned int c = 0; c < gap_bits; c += 16)
               {
                  if (gap_bits - c < 16)
                  {
                     // Add what we need to
                     index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number, index_track,
                                                                gap_default, (gap_bits - c));

                     // then quit
                     total_gap += gap_bits - c;
                     c = gap_bits;
                  }
                  else
                  {
                     {
                        index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number, index_track,
                                                                   gap_default);

                        if (index_track > imge_item.start_bit_pos && index_track < imge_item.start_bit_pos + 16)
                        {

                        }

                        total_gap += 16;
                     }
                  }
               }
            }
            else
            {
               if (forward_stream.size * 2 + backward_stream.size * 2 > gap_bits)
               {
                  // Remove from forward
                  forward_stream.size = (gap_bits - backward_stream.size * 2) / 2;
               }
               else
               {
                  if (forward_stream.size * 2 + backward_stream.size * 2 < gap_bits)
                  {
                     // Adjust :
                     //
                     if (forward_gap && forward_stream.size == 0)
                     {
                        if (backward_gap && backward_stream.size == 0)
                        {
                           forward_stream.size = gap_bits / 4;
                           backward_stream.size = (gap_bits - forward_stream.size * 2) / 2;
                        }
                        else
                        {
                           forward_stream.size = (gap_bits - backward_stream.size * 2) / 2;
                        }
                     }
                     else
                     {
                        if (backward_gap && backward_stream.size == 0)
                        {
                           backward_stream.size = (gap_bits - forward_stream.size * 2) / 2;
                        }
                     }
                  }
               }
               if (forward_gap)
               {
                  if (forward_stream.size == 0)
                  {
                     if (backward_stream.size != 0)
                     {
                        forward_stream.size = gap_bits / 2 - backward_stream.size;
                     }
                     else
                     {
                        forward_stream.size = backward_stream.size = gap_bits / 4;
                        backward_stream.sample = gap_default;
                     }
                     //forwardStream.Sample = gapDefault;
                  }
                  for (unsigned int c = 0; c < forward_stream.size * 2; c += 16)
                  {
                     if (forward_stream.size * 2 - c < 16)
                     {
                        // Add what we need to
                        index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number, index_track,
                                                                   forward_stream.sample,
                                                                   (forward_stream.size * 2 - c));

                        // then quit
                        total_gap += forward_stream.size * 2 - c;
                        c = forward_stream.size * 2;
                     }
                     else
                     {
                        if (forward_stream.sample_size == 8)
                        {
                           index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number,
                                                                      index_track, forward_stream.sample);

                           if (index_track > imge_item.start_bit_pos && index_track < imge_item.start_bit_pos + 16)
                           {
                           }

                           total_gap += 16;
                        }
                        else
                        {
                        }
                     }
                  }
               }
               // Between these two :
               {
                  if (forward_stream.size * 2 + backward_stream.size * 2 < gap_bits)
                  {
                     int nb_bytes = (gap_bits - (forward_stream.size * 2 + backward_stream.size * 2));
                     for (int gp = 0; gp < nb_bytes; gp += 16)
                     {
                        if (gp + 16 > nb_bytes)
                        {
                           // Add what we need to
                           index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number,
                                                                      index_track, 0x4E, (nb_bytes - gp));

                           // then quit
                           total_gap += nb_bytes - gp;
                        }
                        else
                        {
                           index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number,
                                                                      index_track, 0x4E);
                           total_gap += 16;
                        }

                        if (index_track > imge_item.start_bit_pos && index_track < imge_item.start_bit_pos + 16)
                        {
                        }
                     }
                  }
               }

               // Backward stream ?
               if (backward_gap)
               {
                  // First byte (or any part of it)
                  unsigned int c = 0;
                  if (backward_stream.size % 8 != 0)
                  {
                     // TODO
                     c = backward_stream.size % 8;
                     index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number, index_track,
                                                                backward_stream.sample, c);
                  }
                  for (; c < backward_stream.size * 2; c += 16)
                  {
                     if (backward_stream.size * 2 - c < 16)
                     {
                        // Add what we need to
                        index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number, index_track,
                                                                   backward_stream.sample,
                                                                   (backward_stream.size * 2 - c));

                        // then quit
                        total_gap += backward_stream.size * 2 - c;
                        c = backward_stream.size * 2;
                     }
                     else
                     {
                        if (backward_stream.sample_size == 8)
                        {
                           index_track = disk->AddByteToSpecificTrack(imge_item.side, imge_item.track_number,
                                                                      index_track, backward_stream.sample);

                           if (index_track > imge_item.start_bit_pos && index_track < imge_item.start_bit_pos + 16)
                           {
                           }

                           total_gap += 16;
                        }
                        else
                        {
                        }
                     }
                  }
               }
            }
            if (total_gap != gap_bits)
            {
            }
            //else
            total_total += total_gap;
         }

         // Check index / size
         if (index_track != imge_item.start_bit_pos || total_total != disk->side_[imge_item.side].tracks[imge_item.
            track_number].size)
         {
         }

         // Density protections : rebuild the cell timing of the original disk and measure it like a
         // flux dump, so HFE / SCP output made from an IPF keeps the protection. Type 5 (Copylock ST) :
         // block 5 +5%. Types 6 / 7 (Speedlock key) : block 1 +5%, block 2 -5%, as measured on the
         // real disks (the SPS library plays +10%/-10% or +5% only, an approximation of the same key).
         IDisk::MFMTrack& mt = disk->side_[imge_item.side].tracks[imge_item.track_number];
         if ((imge_item.density == 5 || imge_item.density == 6 || imge_item.density == 7) && mt.bitfield && mt.size)
         {
            const unsigned int n = mt.size;
            std::vector<unsigned short> cell_ns(n, 2000);
            std::vector<unsigned char> bits(n);
            for (unsigned int c = 0; c < n; c++) bits[c] = mt.bitfield[c] & 1;
            unsigned int pos = imge_item.start_bit_pos % n;
            for (size_t b = 0; b < block_spans.size(); b++)
            {
               int adj = 0;
               if (imge_item.density == 5 && b == 5) adj = 100;
               if ((imge_item.density == 6 || imge_item.density == 7) && b == 1) adj = 100;
               if ((imge_item.density == 6 || imge_item.density == 7) && b == 2) adj = -100;
               for (unsigned int c = 0; c < block_spans[b].first; c++)
                  if (adj) cell_ns[(pos + c) % n] = static_cast<unsigned short>(2000 + adj);
               pos = (pos + block_spans[b].first + block_spans[b].second) % n;
            }
            IDisk::MeasureSectorDensity(mt, bits.data(), n, cell_ns.data());
         }
      }
   }

   return true;
}

// decompress CT Raw dump format
int CAPSFile::DecompressData(IDisk* disk, DWORD ctype, DWORD cyl, DWORD head, DataChunks imageItem)
{
   // Get the DATA buffer :

   // First PACK size
   int offset = 0;
   int density_buf_size = ExtractInt(&imageItem.buffer[offset]);
   offset += 4;
   // Second PACK size
   int track_buf_size = ExtractInt(&imageItem.buffer[offset]);
   offset += 4;

   // First pack is density information
   unsigned int* density = ExtractDensity(&imageItem.buffer[offset], density_buf_size, ctype, cyl, head);
   unsigned int nb_density = ExtractInt(&imageItem.buffer[offset + 4]) >> 2;
   offset += density_buf_size;

   // Second one is track informations
   ExtractTrack(disk, &imageItem.buffer[offset], track_buf_size, ctype, cyl, head);

   // The density pack holds one value per MFM byte (8 cells) of the first revolution, 16 = nominal
   // 2us cells. Turn it into per-cell timing so density protections (Copylock ST) are detected
   // exactly as for SCP / KryoFlux dumps.
   if (head < 2 && cyl < disk->side_[head].nb_tracks && disk->side_[head].tracks[cyl].nb_revolutions > 0)
   {
      IDisk::MFMTrack& mfm = disk->side_[head].tracks[cyl];
      unsigned int nb_cells = mfm.revolution[0].size;
      if (nb_density > 0 && nb_cells > 0)
      {
         unsigned short* cell_ns = new unsigned short[nb_cells];
         for (unsigned int c = 0; c < nb_cells; c++)
         {
            unsigned int d = density[(std::min)(c / 8, nb_density - 1)];
            cell_ns[c] = static_cast<unsigned short>(d * 125); // 16 -> 2000 ns
         }
         IDisk::MeasureSectorDensity(mfm, mfm.revolution[0].bitfield, nb_cells, cell_ns);
         delete[] cell_ns;
      }
   }
   delete[] density;

   return 0;
}

unsigned int* CAPSFile::ExtractDensity(unsigned char* buffer, int density_buf_size, DWORD ctype, DWORD cyl, DWORD head)
{
   unsigned int usize = ExtractInt(&buffer[4]);
   unsigned int ucrc = ExtractInt(&buffer[8]);; // CRC on uncompressed data
   unsigned int csize = ExtractInt(&buffer[12]);; // compressed size in bytes
   unsigned int ccrc = ExtractInt(&buffer[16]);; // CRC on compressed data
   unsigned int hcrc = ExtractInt(&buffer[20]);; // CRC on header calculated as hcrc=0

   // density
   unsigned int* buf = new unsigned int[usize >> 2];

   // Ptr to end of buffer
   unsigned int* mem = &buf[usize >> 2];
   unsigned int offset_source = density_buf_size;

   // Decode data :
   while (mem > buf)
   {
      int size, ofs = 0;
      unsigned char cb0 = buffer[--offset_source];

      switch (cb0 & 0x03)
      {
      case 0x0: // data block
         // 14 or 4 bit size
         if (cb0 & 0x8)
         {
            size = ((cb0 << 4) & 0xF00) | (buffer[--offset_source]);
         }
         else
         {
            size = (cb0 >> 4) + 1;
         }
         if (cb0 & 0x4)
         {
            // 4 byte per value
            while (size--)
            {
               unsigned int data;
               data = buffer[--offset_source];
               data = (data << 8) | buffer[--offset_source];
               data = (data << 8) | buffer[--offset_source];
               data = (data << 8) | buffer[--offset_source];
               *--mem = data;
            }
         }
         else
         {
            while (size--)
            {
               *--mem = buffer[--offset_source];
            }
         }
         continue;

      case 0x1: // copy block, 8bit offset, 6 bit size
         size = (cb0 >> 2) + 1;
         ofs = buffer[--offset_source] + 1;
         break;
      case 0x2: // copy block, 16 bit offset, 6 bit size
         size = (cb0 >> 2) + 1;
         ofs = buffer[--offset_source];
         ofs = (ofs << 8) | buffer[--offset_source];
         break;
      case 0x3: // Copy block, 16 bit offset, 14 bit size
         size = ((cb0 << 6) & 0x3F00) | buffer[--offset_source];
         ofs = buffer[--offset_source];
         ofs = (ofs << 8) | buffer[--offset_source];
         break;
      }

      while (size--)
      {
         mem--;
         *mem = mem[ofs];
      }
   }


   if (cyl == 0x12)
   {
      int nbDens = (usize >> 2);
      for (int i = 0; i < nbDens; i++)
      {
         if (buf[i] >= 0x11 || buf[i] <= 0x0C)
         {
            char buff[128];
            sprintf(buff, " index = %i - Valeur = %2.2X ", i, buf[i]);
            OutputDebugString (buff);
         }
      }
   }

   return buf;
}

////////////////////////////
// Extract CTRAW track
int CAPSFile::ExtractTrack(IDisk* disk, unsigned char* buffer, int track_buf_size, DWORD ctype, DWORD cyl, DWORD head)
{
   // PACK HEADER
   unsigned int usize = ExtractInt(&buffer[4]);
   unsigned int ucrc = ExtractInt(&buffer[8]);; // CRC on uncompressed data
   unsigned int csize = ExtractInt(&buffer[12]);; // compressed size in bytes
   unsigned int ccrc = ExtractInt(&buffer[16]);; // CRC on compressed data
   unsigned int hcrc = ExtractInt(&buffer[20]);; // CRC on header calculated as hcrc=0


   delete[]disk->side_[head].tracks[cyl].revolution[0].bitfield;
   delete[]disk->side_[head].tracks[cyl].revolution;


   disk->side_[head].tracks[cyl].nb_revolutions = buffer[24];
   disk->side_[head].tracks[cyl].revolution = new IDisk::Revolution[disk->side_[head].tracks[cyl].nb_revolutions];

   int offset = 25;
   for (unsigned int rev = 0; rev < disk->side_[head].tracks[cyl].nb_revolutions; rev++)
   {
      disk->side_[head].tracks[cyl].revolution[rev].size = ExtractShort(&buffer[offset]) * 8;
      disk->side_[head].tracks[cyl].revolution[rev].bitfield = new unsigned char[disk->side_[head].tracks[cyl].
         revolution[rev].size];
      offset += 2;
   }

   // First track
   unsigned int track_offset = 0;
   if (disk->side_[head].tracks[cyl].nb_revolutions > 0)
   {
      for (unsigned int i = 0; i < disk->side_[head].tracks[cyl].revolution[0].size / 8; i++)
      {
         track_offset = disk->AddMFMByteToSpecificTrack(head, cyl, 0, track_offset, buffer[offset++], 8);
      }
   }

   for (unsigned int rev = 1; rev < disk->side_[head].tracks[cyl].nb_revolutions; rev++)
   {
      track_offset = 0;
      while (track_offset < disk->side_[head].tracks[cyl].revolution[rev].size && offset < track_buf_size)
      {
         // Lecture de la taille
         unsigned int size = buffer[offset++];
         if (size & 0x80)
         {
            // Copy block
            // 3 bit shift
            int shift = ((size >> 4) & 7);

            // 12 bits size
            size = ((size & 0xF) << 8) | buffer[offset++];

            // 16 bits offset
            unsigned int ofs = ((buffer[offset] << 8) | (buffer[offset + 1]));
            offset += 2;
            ofs *= 8;
            unsigned char* buf = &disk->side_[head].tracks[cyl].revolution[0].bitfield[ofs];

            if (shift)
            {
               // Shift; "shift" bit :
               size *= 8;
               buf += (8 - shift);
               if (track_offset + size > disk->side_[head].tracks[cyl].revolution[rev].size)
               {
               }
               if (ofs + (8 - shift) + size > disk->side_[head].tracks[cyl].revolution[0].size)
               {
               }
               memcpy(&disk->side_[head].tracks[cyl].revolution[rev].bitfield[track_offset], buf, size);
               track_offset += size;
            }
            else
            {
               size *= 8;
               if (track_offset + size > disk->side_[head].tracks[cyl].revolution[rev].size)
               {
                  // todo ?
               }
               if (ofs + (8 - shift) + size > disk->side_[head].tracks[cyl].revolution[0].size)
               {
                  // todo ?
               }


               memcpy(&disk->side_[head].tracks[cyl].revolution[rev].bitfield[track_offset], buf, size);
               track_offset += size;
            }
         }
         else
         {
            // Data block, 15 bit size
            size = (size << 8) | buffer[offset++];

            for (size; size > 0; size--)
            {
               track_offset = disk->AddMFMByteToSpecificTrack(head, cyl, rev, track_offset, buffer[offset++], 8);
            }
         }
      }
   }

   return 0;
}

int CAPSFile::CreateIpf(IDisk* disk)
{
   // Clear values
   memset(&caps_header_, 0, sizeof(caps_header_));
   memset(&caps_info_, 0, sizeof(caps_info_));
   Clear();

   // Now create it !
   ////////////////////
   // CAPS
   memcpy(caps_header_.type, "CAPS", 4);
   caps_header_.length = 12;
   caps_header_.crc = 0;
   Swap((unsigned char*)&caps_header_.length, caps_header_.length - 4);
   unsigned int crc = CRC::ComputeCrc32(0xEDB88320, (unsigned char*)&caps_header_, 12);
   caps_header_.crc = SwapInt(crc);

   ///////////////////////
   // INFO Chunk
   memset(&caps_info_, 0, sizeof(caps_info_));
   memcpy(caps_info_.header.type, "INFO", 4);
   caps_info_.header.length = 96;
   caps_info_.header.crc = 0;
   caps_info_.media_type = 1;
   caps_info_.encoder_type = 2;
   caps_info_.encoder_rev = 1;
   caps_info_.file_key = 0;
   caps_info_.file_rev = 1;
   caps_info_.origin_crc32 = 0; // TODO ??
   caps_info_.track_min = 0;
   caps_info_.side_min = 0;
   // Single-sided ST dumps are common : record the real side count
   const int nb_sides_out = (disk->nb_sides_ >= 2 && disk->side_[1].nb_tracks > 0) ? 2 : 1;
   const bool atari_st = (disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST);
   caps_info_.side_max = atari_st ? (nb_sides_out - 1) : 1; // CPC : keep historical behaviour (always 2 sides)
   caps_info_.creation_date = 0; // TODO
   caps_info_.creation_time = 0; // TODO
   caps_info_.platform_1 = atari_st ? 2 : 4; // IPF platform IDs : 2 = Atari ST, 4 = Amstrad CPC
   caps_info_.disk_number = 0;
   caps_info_.creator_id = 0; // TODO

   // Unset unformatted tracks at the end

   // Now, fill the tracks ! - Force 2 sides
   int datakey = 1;
   int maxtrack = disk->side_[0].nb_tracks > disk->side_[1].nb_tracks
                     ? disk->side_[0].nb_tracks
                     : disk->side_[1].nb_tracks;

   for (int tr = 0; tr < maxtrack; tr++)
   {
      for (int side = 0; side <= static_cast<int>(caps_info_.side_max); side++)
      {
         CreateData(disk, side, tr, datakey++);
      }
   }

   caps_info_.track_max = maxtrack - 1;
   Swap((unsigned char*)&caps_info_.header.length, caps_info_.header.length - 4);
   crc = CRC::ComputeCrc32(0xEDB88320, (unsigned char*)&caps_info_, sizeof(caps_info_));
   caps_info_.header.crc = SwapInt(crc);


   return 0;
}


int CAPSFile::AddDataRecordInBits(StreamRecording* record, unsigned char type, unsigned int nb_bits_to_add,
                                  unsigned char* buffer)
{
   unsigned char size = 0;
   int tmp = nb_bits_to_add;
   while (tmp > 0)
   {
      tmp >>= 8;
      size++;
   }
   while (record->size - record->offset <= (size + 2 + nb_bits_to_add))
   {
      unsigned char* tmp_buffer = record->buffer;
      record->buffer = new unsigned char[record->size * 2];
      memcpy(record->buffer, tmp_buffer, record->size);
      record->size = record->size * 2;
      delete[]tmp_buffer;
   }

   if (size > 0)
   {
      record->buffer[record->offset++] = (size << 5) | type;
      for (int c = 0; c < size; c++)
      {
         record->buffer[record->offset++] = (nb_bits_to_add >> (8 * (size - (c + 1))));
      }


      unsigned short b = 0;
      int dec = 15;
      for (unsigned int i = 0; i < nb_bits_to_add; i++)
      {
         b |= ((buffer[i] & 1) << dec);
         dec--;
      }

      // Sample now
      record->buffer[record->offset++] = ((b >> 8) & 0xFF); // Sample
      if (nb_bits_to_add > 8)
      {
         record->buffer[record->offset++] = ((b) & 0xFF); // Sample
      }
   }
   return 0;
}

int CAPSFile::AddDataRecord(StreamRecording* record, unsigned char type, int nb_byte_to_add, unsigned char* buffer,
                            bool byte_size)
{
   unsigned int size = 0;
   int tmp = nb_byte_to_add;
   int nbByte = nb_byte_to_add;
   while (tmp > 0)
   {
      tmp >>= 8;
      size++;
   }

   while (record->size - record->offset <= (size + 2 + nb_byte_to_add))
   {
      unsigned char* tmp = record->buffer;
      record->buffer = new unsigned char[record->size * 2];
      memcpy(record->buffer, tmp, record->size);
      record->size = record->size * 2;
      delete[]tmp;
   }
   if (size > 0)
   {
      record->buffer[record->offset++] = (size << 5) | type;
      for (unsigned int c = 0; c < size; c++)
      {
         record->buffer[record->offset++] = (nbByte >> (8 * (size - (c + 1))));
      }

      // Sample now
      if (type == 5)
      {
         // Nothing to add !
         // TODO : Check
      }
      else
      {
         if (byte_size)
         {
            for (int c = 0; c < nb_byte_to_add; c++)
            {
               record->buffer[record->offset++] = buffer[c]; // Sample
            }
         }
         else
         {
            int c = 0;
            while (nb_byte_to_add > 0)
            {
               if (nb_byte_to_add > 8)
               {
                  record->buffer[record->offset++] = buffer[c++]; // Sample
                  nb_byte_to_add -= 8;
               }
               else
               {
                  record->buffer[record->offset++] = buffer[c++]; // Sample
                  nb_byte_to_add = 0;
               }
            }
         }
      }
   }
   return 0;
}

int CAPSFile::AddShortStreamRecord(StreamRecording* record, unsigned char* buffer, unsigned int offset_begin,
                                   unsigned int offset_end)
{
   // Record less than 16 bits - So MFM bits !
   AddDataRecordInBits(record, 1, (offset_end - offset_begin), &buffer[offset_begin]);
   record->buffer[record->offset++] = 0x00; // End of data
   return 0;
}

bool CAPSFile::AddStreamRecord(StreamRecording* record, unsigned char* buffer, unsigned int offset_begin,
                               unsigned int offset_end, unsigned int size_of_track)
{
   unsigned char byte;
   // first byte is sync
   // First is always sync block
   unsigned char sync_buf[6] = {0};
   unsigned char buffer_short[2]; // FC, CRC, etc
   int offset = 0;
   int sector_size = 0;
   // Record eveything between of odfsetBegin and offset_end
   bool byte_size = true;

   // Now, compute if byte or bit
   int current_offset = offset_begin;
   bool end = false;
   while (!end && byte_size)
   {
      int next_sync = FindNextSector(buffer, offset_end, current_offset, 6);

      // Ok, get ready for bitsize everywhere !
      if (next_sync == -1)
      {
         next_sync = offset_end;
         end = true;
      }
      if ((next_sync - current_offset) % 16 != 0)
         byte_size = false;
      else
      {
         if (next_sync != current_offset)
         {
            // Check for bit/byte size : Size is not enough
            for (int i = offset_begin; i < next_sync && byte_size; i += 16)
            {
               if (i < next_sync)
               {
                  byte_size = IDisk::MFMCorrect(&buffer[i], (i > 0) ? (buffer[i - 1]) : 0);
                  if (i > 0)
                  {
                     if ((buffer[i - 16 + 15] == 0 && buffer[i] == 0 && (buffer[i + 1] == 0 || (i > 2 && buffer[i - 16 +
                           14] == 0)))
                        || (buffer[i - 16 + 15] == 1 && buffer[i] == 1))
                     {
                        byte_size = false;
                     }
                  }
                  else
                  {
                     if ((buffer[size_of_track - 1] == 0 && buffer[i] == 0 && (buffer[i + 1] == 0 || (i > 2 && buffer[
                           size_of_track - 16 + 14] == 0)))
                        || (buffer[size_of_track - 1] == 1 && buffer[i] == 1))
                     {
                        byte_size = false;
                     }
                  }
               }
               else
                  byte_size = false;
            }
         }
         else
         {
            byte_size = false;
         }
      }
      // PROPAGATE NOW !
      current_offset = next_sync + 1;
   }


   int size_to_add;
   while (offset_begin < offset_end)
   {
      int remainingBits = 0;
      if (offset_end - offset_begin < 6 * 8)
      {
         // Record what we can, then end
         for (unsigned int i = 0; i < (offset_end - offset_begin) / 8; i++)
         {
            byte = IDisk::ConvertToByte(&buffer[offset_begin + offset]);
            sync_buf[i] = byte;
            offset += 8;
         }

         size_to_add = (byte_size) ? (offset_end - offset_begin) / 8 : (offset_end - offset_begin);
         AddDataRecord(record, 1, size_to_add, sync_buf, byte_size);
         record->buffer[record->offset++] = 0x00; // End of data
         return offset_end - offset_begin;
      }
      // Record what we can, then end
      // Weak bits ?
      // TODO
      if (IDisk::NoWeakBits(buffer, offset_begin, 6 * 8))
      {
         for (int i = 0; i < 6; i++)
         {
            byte = IDisk::ConvertToByte(&buffer[offset_begin]);
            sync_buf[i] = byte;
            offset_begin += 8;
         }
         size_to_add = (byte_size) ? 6 : 48;
         AddDataRecord(record, 1, size_to_add, sync_buf, byte_size);
      }

      // Next byte
      if (offset_end > offset_begin)
      {
         // Weak bits ?
         bool byte_read = false;
         if (offset_begin > 0 && (offset_end - offset_begin >= 16) && IDisk::NoWeakBits(buffer, offset_begin, 16) &&
            IDisk::MFMCorrect(&buffer[offset_begin], (offset_begin > 0) ? (buffer[offset_begin - 1]) : 0))
         {
            buffer_short[0] = IDisk::ConvertMFMToByte(&buffer[offset_begin]);
            byte_read = true;
            offset_begin += 16;
            size_to_add = (byte_size) ? 1 : 8;
            AddDataRecord(record, 2, size_to_add, buffer_short, byte_size);
         }
         // Check the next sync byte
         unsigned int next_sync = FindNextSector(buffer, offset_end, offset_begin, 6);
         if (next_sync == -1)
            next_sync = offset_end;


         if (next_sync > offset_begin)
         {
            // Check for weak bits or incorrect MFM in CHRN area
            int sizeToCheck = (next_sync - offset_begin < 6 * 16) ? (next_sync - offset_begin) : 6 * 16;
            if (IDisk::NoWeakBits(buffer, offset_begin, sizeToCheck) == false)
               byte_read = false;
            for (int i = 0; i < sizeToCheck; i++)
            {
               if (offset_begin + i * 16 + 16 > next_sync)
                  byte_read = false;
               else
               {
                  if (IDisk::MFMCorrect(&buffer[offset_begin + i * 16],
                                        (offset_begin + i * 16 > 0) ? buffer[offset_begin + i * 16 - 1] : 0) == false)
                  {
                     byte_read = false;
                  }
               }
            }


            // between these two (or the first and the end), it's pure data
            if (byte_read && buffer_short[0] == 0xFE)
            {
               // CHRN + CRC
               // Weak bits ?
               // TODO

               if (next_sync - offset_begin < 6 * 16)
               {
                  for (unsigned int i = 0; i < (next_sync - offset_begin) / 16; i++)
                  {
                     byte = IDisk::ConvertMFMToByte(&buffer[offset_begin + offset]);
                     sync_buf[i] = byte;
                     offset += 16;
                  }
                  size_to_add = (byte_size) ? (next_sync - offset_begin) / 8 : (next_sync - offset_begin);
                  AddDataRecord(record, 1, size_to_add, sync_buf, byte_size);
                  offset_begin += offset /** 16*/;
               }
               else
               {
                  unsigned char buffer_chrn[6];
                  for (int i = 0; i < 6; i++)
                  {
                     byte = IDisk::ConvertMFMToByte(&buffer[offset_begin]);
                     buffer_chrn[i] = byte;
                     offset_begin += 16;
                  }
                  // Keep size for next run
                  sector_size = (buffer_chrn[3] << 8);

                  size_to_add = (byte_size) ? 6 : 6 * 8;
                  AddDataRecord(record, 2, size_to_add, buffer_chrn, byte_size);
               }
            }

            // DATA - sector_size in theory, with size of buffer in logic
            while (next_sync > offset_begin)
            {
               // While there is something to do !
               // Search for next weak bits
               int next_weak_bits_index = GetNextFuzzyBits(buffer, offset_begin, next_sync, true);

               // Normal Data : From offset_begin to next_weak_bits_index or next_sync if nothing found)
               int end_of_weak_range = next_sync;
               next_sync = (next_weak_bits_index == -1) ? next_sync : next_weak_bits_index;

               int lastbits = (next_sync - offset_begin) % 16;

               int max_size_mfm_test = (next_sync - offset_begin) / 16;

               bool mfm_is_correct = true;
               for (unsigned int i = offset_begin; i < next_sync && mfm_is_correct; i += 16)
               {
                  if (i < next_sync && i + 16 < next_sync)
                  {
                     mfm_is_correct = IDisk::MFMCorrect(&buffer[i], (i > 0) ? buffer[i - 1] : 0);
                     if (i > 0)
                     {
                        if ((buffer[i - 16 + 15] == 0 && buffer[i] == 0 && (buffer[i + 1] == 0 || (buffer[i - 16 + 14]
                              == 0)))
                           || (buffer[i - 16 + 15] == 1 && buffer[i] == 1)
                        )
                        {
                           mfm_is_correct = false;
                        }
                     }
                     else
                     {
                        // todo !!!
                     }
                  }
                  else
                     mfm_is_correct = false;
               }

               if ((lastbits != 0) || (!mfm_is_correct)) // todo : Check this !!
               {
                  int max_size = (next_sync - offset_begin);

                  // MFM so everything is recorded

                  unsigned char* buffer_data = new unsigned char[(next_sync - offset_begin) / 8 + 2];
                  memset(buffer_data, 0, (next_sync - offset_begin) / 8 + 2);

                  for (int i = 0; i < max_size / 8; i++)
                  {
                     buffer_data[i] = IDisk::ConvertToByte(&buffer[offset_begin]);
                     offset_begin += 8;
                  }
                  // Remaining bits
                  int dec = 7;
                  int buf_indx = max_size / 8;
                  lastbits = max_size - (max_size / 8) * 8;
                  for (int i = 0; i < lastbits; i++)
                  {
                     buffer_data[buf_indx] |= ((buffer[offset_begin++] & 1) << dec);
                     if (dec == 0)
                     {
                        dec = 8;
                        buf_indx++;
                     }
                     dec--;
                     //offset_begin ++;
                  }
                  size_to_add = max_size;
                  AddDataRecord(record, 1, size_to_add, buffer_data, false);
                  delete[]buffer_data;
               }
               else
               {
                  int maxSize = (next_sync - offset_begin) / 16;
                  unsigned char* buffer_data = new unsigned char[maxSize];

                  for (int i = 0; i < maxSize; i++)
                  {
                     buffer_data[i] = IDisk::ConvertMFMToByte(&buffer[offset_begin]);
                     offset_begin += 16;
                  }
                  // Remaining bits
                  size_to_add = (byte_size) ? maxSize : (maxSize * 8);
                  AddDataRecord(record, 2, size_to_add, buffer_data, byte_size);
                  delete[]buffer_data;
               }
               // Add weak rang (if any) : From next_sync to end_of_weak_range
               if (next_sync != end_of_weak_range)
               {
                  // Search for next "end opf weak" range
                  int next_weak_bits_index = GetNextFuzzyBits(buffer, offset_begin, end_of_weak_range, false);
                  if (next_weak_bits_index != -1)
                  {
                     AddDataRecord(record, 5, next_weak_bits_index - next_sync, 0, false);
                  }
                  else
                  {
                     AddDataRecord(record, 5, end_of_weak_range - next_sync, 0, false);
                  }


                  offset_begin = next_weak_bits_index;
                  next_sync = end_of_weak_range;
               }
            }
            // Last bits
            // TODO
         }
         offset_begin = next_sync - remainingBits;
      }

      // Check that's MFM does not need to be recorded
      // TODO
      // Check for weak bit
      // TODO
   }
   record->buffer[record->offset++] = 0x00; // End of data

   return byte_size;
}


bool CAPSFile::compact_ = true;
int CAPSFile::speedlock_density_ = 7;

static void AppendBytes(unsigned char*& rbuf, unsigned int& rsize, unsigned int& roff, const std::vector<unsigned char>& bytes)
{
   while (rsize - roff <= bytes.size() + 8)
   {
      unsigned char* tmp = rbuf;
      rbuf = new unsigned char[rsize * 2];
      memcpy(rbuf, tmp, rsize);
      rsize = rsize * 2;
      delete[] tmp;
   }
   memcpy(&rbuf[roff], bytes.data(), bytes.size());
   roff += static_cast<unsigned int>(bytes.size());
}

// Stream element : header (size width << 5 | type), size big endian, payload
static void PushElement(std::vector<unsigned char>& out, unsigned char type, unsigned int size,
                        const unsigned char* payload = nullptr, size_t payload_len = 0)
{
   unsigned char width = 1;
   while (width < 4 && (size >> (8 * width)) != 0) width++;
   out.push_back(static_cast<unsigned char>((width << 5) | type));
   for (int b = width - 1; b >= 0; b--) out.push_back(static_cast<unsigned char>(size >> (8 * b)));
   if (payload) out.insert(out.end(), payload, payload + payload_len);
}

static bool IsSync4489x3(const unsigned char* cells, unsigned int pos, unsigned int end)
{
   static const unsigned short sync = 0x4489;
   if (pos + 48 > end) return false;
   for (unsigned int i = 0; i < 48; i++)
   {
      const unsigned char cell = cells[pos + i];
      if (cell > 1 || cell != ((sync >> (15 - (i & 15))) & 1)) return false;
   }
   return true;
}

// Decode one MFM byte at pos. Returns false if a cell is weak or a clock bit is wrong.
static bool DecodeMfmByte(const unsigned char* cells, unsigned int pos, unsigned char& value, bool& weak)
{
   value = 0;
   weak = false;
   bool ok = true;
   unsigned char prev = (pos > 0) ? (cells[pos - 1] & 1) : 0;
   for (unsigned int i = 0; i < 8; i++)
   {
      const unsigned char c = cells[pos + 2 * i], d = cells[pos + 2 * i + 1];
      if (c > 1 || d > 1) weak = true;
      const unsigned char data = d & 1, clock = c & 1;
      if (clock != ((prev == 0 && data == 0) ? 1 : 0)) ok = false;
      value = static_cast<unsigned char>((value << 1) | data);
      prev = data;
   }
   return ok && !weak;
}


// Stream block : sync run + decoded bytes (weak bytes as fuzzy) while the MFM stays valid.
// Returns the cell where the stream stops (begin when nothing could be decoded).
static unsigned int EncodeStreamBlock(std::vector<unsigned char>& ds, const unsigned char* cells, unsigned int begin,
                                      unsigned int end, unsigned int nb_syncs)
{
   std::vector<unsigned char> sync_bytes;
   for (unsigned int i = 0; i < nb_syncs; i++) { sync_bytes.push_back(0x44); sync_bytes.push_back(0x89); }
   PushElement(ds, 1, static_cast<unsigned int>(sync_bytes.size()), sync_bytes.data(), sync_bytes.size());
   unsigned int q = begin + 16 * nb_syncs;
   std::vector<unsigned char> run;
   unsigned int fuzzy = 0;
   unsigned char v;
   bool weak;
   while (q + 16 <= end)
   {
      const bool ok = DecodeMfmByte(cells, q, v, weak);
      if (weak)
      {
         if (!run.empty()) { PushElement(ds, 2, static_cast<unsigned int>(run.size()), run.data(), run.size()); run.clear(); }
         fuzzy++;
      }
      else if (ok)
      {
         if (fuzzy) { PushElement(ds, 5, fuzzy); fuzzy = 0; }
         run.push_back(v);
      }
      else break;
      q += 16;
   }
   if (!run.empty()) PushElement(ds, 2, static_cast<unsigned int>(run.size()), run.data(), run.size());
   if (fuzzy) PushElement(ds, 5, fuzzy);
   ds.push_back(0);
   return q;
}

bool CAPSFile::AddCompactBlock(StreamRecording* data, StreamRecording* gap, const unsigned char* cells,
                               unsigned int begin, unsigned int end, BlockDescriptor& block_descriptor,
                               unsigned int min_gap)
{
   static const unsigned char sync_bytes[6] = {0x44, 0x89, 0x44, 0x89, 0x44, 0x89};
   std::vector<unsigned char> ds, gs;
   unsigned char v;
   bool weak;

   // ID field : A1 A1 A1 FE C H R N CRC CRC
   if (!IsSync4489x3(cells, begin, end)) return false;
   unsigned int p = begin + 48;
   if (p + 7 * 16 > end) return false;
   unsigned char id[7];
   for (int i = 0; i < 7; i++, p += 16)
   {
      if (!DecodeMfmByte(cells, p, id[i], weak)) return false;
   }
   if (id[0] != 0xFE) return false;
   PushElement(ds, 1, 6, sync_bytes, 6);
   PushElement(ds, 2, 7, id, 7);

   // Data field : look for its sync within a normal ID->data gap, byte aligned
   unsigned int data_end = p;
   unsigned int dsync = 0;
   for (unsigned int q = p; q + 48 <= end && q <= p + 60 * 16; q += 16)
   {
      if (IsSync4489x3(cells, q, end)) { dsync = q; break; }
   }
   if (dsync != 0)
   {
      std::vector<unsigned char> mid;
      for (unsigned int q = p; q < dsync; q += 16)
      {
         if (!DecodeMfmByte(cells, q, v, weak)) return false;
         mid.push_back(v);
      }
      unsigned int q = dsync + 48;
      unsigned char mark;
      if (q + 16 > end || !DecodeMfmByte(cells, q, mark, weak) || mark < 0xF8 || mark > 0xFB) return false;
      if ((id[4] & 0xFF) > 3) return false; // unusual sector sizes : keep the exact (legacy) encoding
      const unsigned int nb = 1 + (128u << id[4]) + 2;
      if (q + nb * 16 > end) return false;

      if (!mid.empty()) PushElement(ds, 3, static_cast<unsigned int>(mid.size()), mid.data(), mid.size());
      PushElement(ds, 1, 6, sync_bytes, 6);
      // Mark + data + CRC, with fuzzy runs for weak bytes
      std::vector<unsigned char> run;
      unsigned int fuzzy = 0;
      for (unsigned int i = 0; i < nb; i++, q += 16)
      {
         const bool ok = DecodeMfmByte(cells, q, v, weak);
         if (weak)
         {
            if (!run.empty()) { PushElement(ds, 2, static_cast<unsigned int>(run.size()), run.data(), run.size()); run.clear(); }
            fuzzy++;
            continue;
         }
         if (!ok) return false; // bad clock bits inside data : not plain MFM, keep it exact
         if (fuzzy) { PushElement(ds, 5, fuzzy); fuzzy = 0; }
         run.push_back(v);
      }
      if (!run.empty()) PushElement(ds, 2, static_cast<unsigned int>(run.size()), run.data(), run.size());
      if (fuzzy) PushElement(ds, 5, fuzzy);
      data_end = q;
   }
   ds.push_back(0);

   // Gap up to the next block : fill byte, then 00 bytes just before the next sync
   const unsigned int gap_cells = end - data_end;
   if (gap_cells < min_gap) return false; // the reader needs a gap to round the last block
   unsigned int zeros = 0;
   while (gap_cells >= 16 * (zeros + 1))
   {
      const unsigned int z = end - 16 * (zeros + 1);
      if (!DecodeMfmByte(cells, z, v, weak) || v != 0x00) break;
      zeros++;
   }
   const unsigned int fwd_cells = gap_cells - 16 * zeros;
   unsigned char fill = 0x4E;
   if (fwd_cells >= 16)
   {
      // The forward gap must be one repeated byte. A write splice (the end of the track write, or a
      // sector rewritten later) shifts the pattern by a few cells : allow a couple of such phase slips
      // and a few odd bytes, which the reader regenerates as plain fill, like SPS images.
      unsigned int counts[256] = {0}, total = 0, weak_cells = 0;
      for (unsigned int g = data_end; g < data_end + fwd_cells; g++) if (cells[g] > 1) weak_cells++;
      for (unsigned int g = data_end; g + 16 <= data_end + fwd_cells; g += 16)
      {
         unsigned char b = 0;
         for (unsigned int i = 0; i < 8; i++) b = static_cast<unsigned char>((b << 1) | (cells[g + 2 * i + 1] & 1));
         counts[b]++;
         total++;
      }
      unsigned int best = 0;
      for (int b = 1; b < 256; b++) if (counts[b] > counts[best]) best = b;
      fill = static_cast<unsigned char>(best);

      // Steady-state MFM cells of the fill byte repeated (its own last data bit precedes it)
      unsigned char pattern[16];
      unsigned char prev = fill & 1;
      for (int i = 0; i < 8; i++)
      {
         const unsigned char d = (fill >> (7 - i)) & 1;
         pattern[2 * i] = (prev == 0 && d == 0) ? 1 : 0;
         pattern[2 * i + 1] = d;
         prev = d;
      }
      auto matches = [&](unsigned int pos, unsigned int phase)
      {
         for (unsigned int i = 0; i < 16; i++)
            if ((cells[pos + i] & 1) != pattern[(i + phase) & 15]) return false;
         return true;
      };
      const unsigned int gap_end = data_end + fwd_cells;
      unsigned int phase = 0, slips = 0, junk = 0, pos = data_end;
      while (pos + 16 <= gap_end)
      {
         if (matches(pos, phase)) { pos += 16; continue; }
         bool relocked = false;
         for (unsigned int ph = 0; ph < 16 && !relocked; ph++)
         {
            if (pos + 32 <= gap_end && matches(pos, ph) && matches(pos + 16, ph)) { phase = ph; slips++; relocked = true; }
         }
         if (!relocked) { junk += 16; pos += 16; }
      }
      const bool regular = weak_cells <= 16 && slips <= 2 && junk <= 4 * 16;
      if (!regular && getenv("SUGARCONV_DEBUG_COMPACT"))
      {
         printf("\n gap rejected: cells %u zeros %u weak %u fill %02X slips %u junk %u :", gap_cells, zeros, weak_cells, fill, slips, junk);
         for (unsigned int g = data_end; g + 16 <= gap_end; g += 16)
         {
            unsigned char b = 0;
            for (unsigned int i = 0; i < 8; i++) b = static_cast<unsigned char>((b << 1) | (cells[g + 2 * i + 1] & 1));
            printf(" %02X", b);
         }
      }
      if (!regular) return false;
   }
   PushElement(gs, 2, 8, &fill, 1);
   gs.push_back(0);
   block_descriptor.block_flag = 1;
   if (zeros > 0)
   {
      static const unsigned char zero = 0;
      PushElement(gs, 2, 8, &fill, 1);
      PushElement(gs, 1, zeros * 8);
      PushElement(gs, 2, 8, &zero, 1);
      gs.push_back(0);
      block_descriptor.block_flag |= 2;
   }

   AppendBytes(data->buffer, data->size, data->offset, ds);
   AppendBytes(gap->buffer, gap->size, gap->offset, gs);
   block_descriptor.block_bits = data_end - begin;
   block_descriptor.gap_bits = gap_cells;
   block_descriptor.sps_specific.cell_type = 1;
   block_descriptor.encoder_type = 1;
   block_descriptor.gap_value = 0;
   return true;
}


// Raw block : the cells exactly as they are (data in bits), weak cells as fuzzy runs. Used for the
// sub-blocks of a Speedlock-style density key, where block boundaries fall inside a sector.
static void AddRawBlock(unsigned char*& rbuf, unsigned int& rsize, unsigned int& roff, const unsigned char* cells,
                        unsigned int from, unsigned int to, bool resolve_weak)
{
   std::vector<unsigned char> ds;
   unsigned int p = from;
   while (p < to)
   {
      const bool weak = !resolve_weak && cells[p] > 1;
      unsigned int e = p;
      while (e < to && (!resolve_weak && cells[e] > 1) == weak) e++;
      if (weak)
      {
         PushElement(ds, 5, e - p);
      }
      else
      {
         std::vector<unsigned char> packed((e - p + 7) / 8, 0);
         for (unsigned int i = p; i < e; i++)
            if (cells[i] & 1) packed[(i - p) / 8] |= static_cast<unsigned char>(0x80 >> ((i - p) & 7));
         PushElement(ds, 1, e - p, packed.data(), packed.size());
      }
      p = e;
   }
   ds.push_back(0);
   AppendBytes(rbuf, rsize, roff, ds);
}

int CAPSFile::AddGapRecord(StreamRecording* record, int nb_byte_to_add, unsigned char byte)
{
   unsigned int size = 0;
   nb_byte_to_add *= 8;
   int tmp = nb_byte_to_add;
   while (tmp > 0)
   {
      tmp >>= 8;
      size++;
   }

   if (record->size - record->offset <= (size + 5))
   {
      unsigned char* tmp = record->buffer;

      record->buffer = new unsigned char[record->size * 2];
      memcpy(record->buffer, tmp, record->size);
      record->size = record->size * 2;
      delete[]tmp;
   }
   if (size > 0)
   {
      record->buffer[record->offset++] = (size << 5) | 0x01;
      for (unsigned int c = 0; c < size; c++)
      {
         record->buffer[record->offset++] = ((nb_byte_to_add >> (8 * (size - (c + 1)))) & 0xFF);
      }

      // Sample now
      record->buffer[record->offset++] = (1 << 5) | 0x02;
      record->buffer[record->offset++] = 8;
      record->buffer[record->offset++] = byte; // Sample
      record->buffer[record->offset++] = 0x00; // End of data
   }
   return 0;
}

bool CAPSFile::NoWeakBitInPattern(unsigned char* pattern, unsigned int size_of_pattern)
{
   for (unsigned int i = 0; i < size_of_pattern; i++)
   {
      if ((pattern[i] & (BIT_WEAK | BIT_OPTIONAL)) != 0)
         return false;

      // Check for correctness of MFM
      if (i > 2 &&
         (
            (pattern[i - 1] == 1 && pattern[i - 2] == 1)
            || (pattern[i - 1] == 0 && pattern[i - 2] == 0 && pattern[i] == 0)
               || (pattern[i - 1] == 1 && pattern[i] == 1)
         )
      )
      {
         return false;
      }
   }

   return true;
}

int CAPSFile::CreateData(IDisk* disk, int side, unsigned int tr, int datakey)
{
   // Create IMGE and data structure
   ImgeItem imge_item;
   DataChunks data_chunk;

   bool abort = false;

   StreamRecording gap_data;
   StreamRecording data;
   data.offset = 0;
   data.buffer = new unsigned char[128];
   data.size = 128;

   gap_data.offset = 0;
   gap_data.buffer = new unsigned char[128];
   gap_data.size = 128;

   int gap_offset = 0;
   //unsigned char* pData = new unsigned char[128];
   int max_size_data = 128;

   // Block descriptor vector
   std::vector<BlockDescriptor> list_block_descriptor;


   memset(&imge_item, 0, sizeof(imge_item));
   memset(&data_chunk, 0, sizeof(data_chunk));

   // &disk->m_Disk[side].Tracks[tr]);

   ////////////
   // IMGE
   memcpy(imge_item.header.type, "IMGE", 4);
   imge_item.header.crc = 0;
   imge_item.header.length = 0x50;
   imge_item.track_number = tr;
   imge_item.side = side;
   imge_item.density = 2; // auto
   imge_item.signal_type = 1; // 2us cell
   imge_item.track_byte = 0; //
   imge_item.start_byte_pos = 0;
   imge_item.start_bit_pos = 0;
   imge_item.data_bits = 0;
   imge_item.gap_bits = 0;
   imge_item.track_bits = 0;
   imge_item.block_count = 0;
   imge_item.encoder_process = 0; // 0
   imge_item.track_flags = 0;
   imge_item.data_key = 0;

   // Nothing on this side / track ?
   if (side >= disk->nb_sides_ || tr >= disk->side_[side].nb_tracks)
   {
      imge_item.start_bit_pos = 0;
      imge_item.density = 1;
      abort = true;
      //goto End;//return 0;
   }

   unsigned char* track_field = nullptr;
   if (!abort)
   {
      track_field = disk->side_[side].tracks[tr].bitfield;
      // DATA
      if (track_field == NULL)
      {
         imge_item.start_bit_pos = 0;
         imge_item.density = 1;
         abort = true;
         //goto End;//return 0;
      }
   }

   unsigned int startbitpos = 0;
   // Construct DATA associated :
   // Find begining of track

   // Find the 0x5224 0x5224 0x5224  first sync
   unsigned int search_offset = 0;
   unsigned int current_recored_offset = 0;

   unsigned char pattern_byte[16];
   bool starts_with_iam = false;
   bool stream_track = false;
   int stream_sync = -1;
   unsigned int stream_nb_syncs = 0;

   if (!abort)
   {
      // Only search the 3 x 0x5224 bytes
      search_offset = FindNextPattern(track_field, disk->side_[side].tracks[tr].size, search_offset, pPatternC2, 6);
      if (search_offset != -1)
      {
         starts_with_iam = true;
         imge_item.start_bit_pos = (search_offset != -1) ? search_offset : 0;
         imge_item.start_byte_pos = imge_item.start_bit_pos / 8;
      }
      else
      {
         search_offset = FindNextSector(track_field, disk->side_[side].tracks[tr].size, search_offset + 1, 8);
         if (search_offset != -1)
         {
            imge_item.start_bit_pos = (search_offset != -1) ? search_offset : 0;
            imge_item.start_byte_pos = imge_item.start_bit_pos / 8;
         }
         else if (compact_ && (stream_sync = IDisk::FindStreamSync(disk->side_[side].tracks[tr].bitfield,
                                                              disk->side_[side].tracks[tr].size, stream_nb_syncs)) >= 0)
         {
            // No sector headers, but a sync + data stream : custom-format track
            imge_item.start_bit_pos = static_cast<unsigned int>(stream_sync);
            imge_item.start_byte_pos = imge_item.start_bit_pos / 8;
            stream_track = true;
         }
         else
         {
            // Not a formatted disk : Generate random data
            imge_item.start_bit_pos = 0;
            imge_item.density = 1;
            int dbg = 1;
            //goto End;
            abort = true;
         }
      }
   }

   // Speedlock-style density key (Atari ST) : a slow patch followed by a fast patch inside one
   // sector. IPF density type 6 (Speedlock) makes the reader play block 1 slow and block 2 fast, so
   // start the track at that sector's ID and cut the sector into 4 blocks at the patch edges.
   unsigned int sl_cells[3] = {0, 0, 0}; // slow start, slow/fast boundary, fast end (rotated positions)
   bool speedlock = false;
   if (!abort && compact_ && disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST)
   {
      const IDisk::MFMTrack& mt = disk->side_[side].tracks[tr];
      for (int r = 0; r + 1 < mt.nb_density_runs && !speedlock; r++)
      {
         const IDisk::MFMTrack::DensityRun& slow = mt.density_runs[r];
         const IDisk::MFMTrack::DensityRun& fast = mt.density_runs[r + 1];
         if (slow.field != fast.field || slow.permille < 25 || fast.permille > -25) continue;
         if (fast.byte_start > slow.byte_start + slow.byte_len + 16) continue;
         std::vector<std::pair<unsigned int, unsigned int>> fields;
         IDisk::FindDataFields(mt.bitfield, mt.size, fields);
         if (slow.field >= fields.size()) continue;
         const unsigned int data0 = fields[slow.field].first + 16; // first data byte, after the mark
         // ID sync of that sector : the last "A1 A1 A1 FE" header before its data field
         unsigned int id_sync = (unsigned int)-1;
         {
            static const char* idam = "010001001000100101000100100010010100010010001001" "0101010101010100";
            const unsigned int limit = fields[slow.field].first;
            const unsigned int lo = (limit > 2000) ? limit - 2000 : 0;
            for (unsigned int q = limit > 64 ? limit - 64 : 0; q-- > lo;)
            {
               bool m = true;
               for (unsigned int i = 0; i < 64 && m; i++) m = (mt.bitfield[q + i] & 1) == (idam[i] - '0');
               if (m) { id_sync = q; break; }
            }
         }
         if (id_sync == (unsigned int)-1) continue;
         const unsigned int slow_start = data0 + slow.byte_start * 16;
         const unsigned int mid = data0 + ((slow.byte_start + slow.byte_len + fast.byte_start) / 2) * 16;
         const unsigned int fast_end = data0 + (fast.byte_start + fast.byte_len) * 16;
         if (!(id_sync < slow_start && slow_start < mid && mid < fast_end && fast_end < mt.size)) continue;
         imge_item.start_bit_pos = id_sync;
         imge_item.start_byte_pos = id_sync / 8;
         starts_with_iam = false;
         sl_cells[0] = slow_start - id_sync;
         sl_cells[1] = mid - id_sync;
         sl_cells[2] = fast_end - id_sync;
         speedlock = true;
      }
   }

   if (!abort)
   {
      track_field = new unsigned char[disk->side_[side].tracks[tr].size];
      memcpy(track_field, &disk->side_[side].tracks[tr].bitfield[imge_item.start_bit_pos],
             disk->side_[side].tracks[tr].size - imge_item.start_bit_pos);
      memcpy(&track_field[disk->side_[side].tracks[tr].size - imge_item.start_bit_pos],
             disk->side_[side].tracks[tr].bitfield, imge_item.start_bit_pos);

      // Atari ST : a few unstable cells around the write splice (in the gap after the last data field)
      // are dump noise, not a protection. Keep them as plain gap so the track matches SPS images.
      if (disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST)
      {
         const unsigned int n = disk->side_[side].tracks[tr].size;
         std::vector<std::pair<unsigned int, unsigned int>> fields;
         IDisk::FindDataFields(track_field, n, fields);
         unsigned int last_data_end = fields.empty() ? n : fields.back().second;
         for (unsigned int b = last_data_end; b < n; b++)
         {
            if (track_field[b] <= 1) continue;
            unsigned int e = b;
            while (e < n && track_field[e] > 1) e++;
            if (e - b <= 64) // up to 4 bytes of splice noise
            {
               for (unsigned int c = b; c < e; c++) track_field[c] &= 1;
            }
            b = e;
         }
      }
      if (getenv("SUGARCONV_DEBUG_WEAK"))
      {
         unsigned int n = disk->side_[side].tracks[tr].size;
         for (unsigned int b = 0; b < n; b++)
         {
            if (track_field[b] > 1)
            {
               unsigned int e = b;
               while (e < n && track_field[e] > 1) e++;
               printf("\n weak %02u.%d : cells %u-%u (len %u) of %u", tr, side, b, e, e - b, n);
               b = e;
            }
         }
      }
      search_offset = 0;
      int initial_backward_gap = 0;
      unsigned int offset_gap = disk->side_[side].tracks[tr].size - 16;
      while ((offset_gap > current_recored_offset && memcmp(&track_field[offset_gap], pPattern00, 16) == 0)
         && (offset_gap < 1 || track_field[offset_gap - 1] != 1)
      )
      {
         offset_gap -= 16;
         initial_backward_gap++;
      }

      while (search_offset != -1)
      {
         int nb_backward_gap_bytes = 0;
         // Add it : Descriptor, and data
         BlockDescriptor block_descriptor;
         memset(&block_descriptor, 0, sizeof(block_descriptor));

         block_descriptor.block_flag = 0;
         block_descriptor.data_offset = data.offset;
         block_descriptor.sps_specific.gap_offset = gap_data.offset;


         unsigned int search_next_offset = FindNextSector(track_field, disk->side_[side].tracks[tr].size,
                                                          search_offset + 1, 8);
         const bool last_block = (search_next_offset == (unsigned int)-1);
         if (stream_track && search_offset == 0)
         {
            const unsigned int track_end = disk->side_[side].tracks[tr].size;
            std::vector<unsigned char> ds;
            const unsigned int stream_end = EncodeStreamBlock(ds, track_field, 0, track_end, stream_nb_syncs);
            // block 0 : sync + decoded stream (whole bytes)
            BlockDescriptor b0;
            memset(&b0, 0, sizeof(b0));
            b0.data_offset = data.offset;
            b0.sps_specific.gap_offset = gap_data.offset;
            AppendBytes(data.buffer, data.size, data.offset, ds);
            b0.block_bits = stream_end;
            b0.sps_specific.cell_type = 1;
            b0.encoder_type = 1;
            // the rest of the track (write splice before the sync) : exact cells, with a small gap at
            // the very end so the reader can round the track size
            const unsigned int rest = track_end - stream_end;
            if (rest >= 32)
            {
               b0.gap_bits = 0;
               list_block_descriptor.push_back(b0);
               imge_item.data_bits += b0.block_bits;
               imge_item.block_count++;
               BlockDescriptor b1;
               memset(&b1, 0, sizeof(b1));
               b1.data_offset = data.offset;
               b1.sps_specific.gap_offset = gap_data.offset;
               AddRawBlock(data.buffer, data.size, data.offset, track_field, stream_end, track_end - 16, false);
               b1.block_bits = track_end - 16 - stream_end;
               b1.block_flag = 4;
               b1.sps_specific.cell_type = 1;
               b1.encoder_type = 1;
               b0 = b1;
            }
            // gap : fill with the last decoded value pattern
            std::vector<unsigned char> gs;
            unsigned char fill = 0;
            for (int i = 0; i < 8; i++) fill = static_cast<unsigned char>((fill << 1) | (track_field[track_end - 16 + 2 * i + 1] & 1));
            PushElement(gs, 2, 8, &fill, 1);
            gs.push_back(0);
            AppendBytes(gap_data.buffer, gap_data.size, gap_data.offset, gs);
            b0.gap_bits = (rest >= 32) ? 16 : rest;
            b0.block_flag |= 1;
            list_block_descriptor.push_back(b0);
            imge_item.data_bits += b0.block_bits;
            imge_item.gap_bits += b0.gap_bits;
            imge_item.track_byte += (track_end + 7) / 8;
            imge_item.block_count++;
            stream_tracks_++;
            search_offset = search_next_offset;
            continue;
         }
         if (speedlock && search_offset == 0)
         {
            const unsigned int block_end = (search_next_offset != (unsigned int)-1)
                                              ? search_next_offset : disk->side_[side].tracks[tr].size;
            if (sl_cells[2] < block_end)
            {
               const unsigned int cut[5] = {0, sl_cells[0], sl_cells[1], sl_cells[2], block_end};
               for (int sb = 0; sb < 4; sb++)
               {
                  BlockDescriptor sub;
                  memset(&sub, 0, sizeof(sub));
                  sub.data_offset = data.offset;
                  sub.sps_specific.gap_offset = gap_data.offset;
                  // The key sector itself is stable on the disk : unstable cells there are splice
                  // ambiguity in the dump, so they keep the value read (the rest of the track keeps weak bits)
                  AddRawBlock(data.buffer, data.size, data.offset, track_field, cut[sb], cut[sb + 1], true);
                  sub.block_bits = cut[sb + 1] - cut[sb];
                  sub.gap_bits = 0;
                  sub.block_flag = 4; // sizes in bits
                  sub.sps_specific.cell_type = 1;
                  sub.encoder_type = 1;
                  sub.gap_value = 0x4E;
                  imge_item.data_bits += sub.block_bits;
                  imge_item.track_byte += (sub.block_bits + 7) / 8;
                  list_block_descriptor.push_back(sub);
                  imge_item.block_count++;
               }
               // Speedlock key : type 7 (block 1 +5%) matches the real disks (slow patch +5%, fast patch
               // -5%) best and is what Dragon's Breath needs ; type 6 (+10% / -10%, -speedlock10) is the
               // layout SPS used for Colorado
               imge_item.density = speedlock_density_;
               speedlock_tracks_++;
               search_offset = search_next_offset;
               continue;
            }
         }
         if (compact_)
         {
            const unsigned int block_end = (search_next_offset != (unsigned int)-1)
                                              ? search_next_offset : disk->side_[side].tracks[tr].size;
            if (AddCompactBlock(&data, &gap_data, track_field, search_offset, block_end, block_descriptor,
                                last_block ? 8 : 0))
            {
               imge_item.data_bits += block_descriptor.block_bits;
               imge_item.gap_bits += block_descriptor.gap_bits;
               imge_item.track_byte += (block_descriptor.block_bits + block_descriptor.gap_bits) / 8;
               list_block_descriptor.push_back(block_descriptor);
               imge_item.block_count++;
               compact_blocks_++;
               search_offset = search_next_offset;
               continue;
            }
            legacy_blocks_++;
            if (last_block && block_end - search_offset > 64)
            {
               // Exact cells, with the last byte as the track gap : the SPS reader rounds the track
               // to whole bytes by growing the last gap, which must not be empty
               BlockDescriptor lb;
               memset(&lb, 0, sizeof(lb));
               lb.data_offset = data.offset;
               lb.sps_specific.gap_offset = gap_data.offset;
               AddRawBlock(data.buffer, data.size, data.offset, track_field, search_offset, block_end - 16, false);
               unsigned char fill = 0;
               for (int i = 0; i < 8; i++) fill = static_cast<unsigned char>((fill << 1) | (track_field[block_end - 16 + 2 * i + 1] & 1));
               std::vector<unsigned char> gs;
               PushElement(gs, 2, 8, &fill, 1);
               gs.push_back(0);
               AppendBytes(gap_data.buffer, gap_data.size, gap_data.offset, gs);
               lb.block_bits = block_end - 16 - search_offset;
               lb.gap_bits = 16;
               lb.block_flag = 4 | 1;
               lb.sps_specific.cell_type = 1;
               lb.encoder_type = 1;
               lb.gap_value = fill;
               imge_item.data_bits += lb.block_bits;
               imge_item.gap_bits += lb.gap_bits;
               imge_item.track_byte += (block_end - search_offset + 7) / 8;
               list_block_descriptor.push_back(lb);
               imge_item.block_count++;
               search_offset = search_next_offset;
               continue;
            }
         }
         int end_of_block = 0;
         if (search_next_offset != -1)
         {
            // Next block found : Dont use the '0x00' sync bytes before it - It's the 'backward' gap
            unsigned int offset_gap = search_next_offset - 16;
            nb_backward_gap_bytes = 0;
            while ((offset_gap > search_offset && memcmp(&track_field[offset_gap], pPattern00, 16) == 0)
               && (offset_gap < 1 || track_field[offset_gap - 1] != 1)
            )
            {
               offset_gap -= 16;
               nb_backward_gap_bytes++;
            }
            end_of_block = offset_gap + 16;
         }
         else
         {
            // End block : until the end, then until IMGEitem.start_bit_pos
            nb_backward_gap_bytes = initial_backward_gap;
            end_of_block = disk->side_[side].tracks[tr].size - nb_backward_gap_bytes * 16;
         }

         // What to record : From the next block, until it changed, record the "forward gap"
         memcpy(pattern_byte, &track_field[end_of_block - 16], 16);
         int nb_gap_bytes = 0;
         unsigned int offset_gap = end_of_block - 16;

         // Only if no weak bits in the pattern !
         if (NoWeakBitInPattern(pattern_byte, 16))
         {
            if (pattern_byte[0] == 0
               && pattern_byte[15] == 0
               && (pattern_byte[14] & pattern_byte[1]) == 0
            )
            {
               int dbg = 1;
            }

            while ((offset_gap > search_offset && memcmp(&track_field[offset_gap], pattern_byte, 16) == 0)
               && (((track_field[offset_gap] & track_field[offset_gap - 1]) != 1) || offset_gap - 1 <= search_offset)
               && (offset_gap < 1 || ((track_field[offset_gap - 1] & track_field[offset_gap] & track_field[offset_gap +
                     1]) == 1)
                  && (offset_gap < 2 || (track_field[offset_gap - 1] & track_field[offset_gap] & track_field[offset_gap
                     - 2]) == 1)
               )
            )
            {
               offset_gap -= 16;
               nb_gap_bytes++;
            }
         }
         offset_gap += 16;
         // pPatternByte -> signel byte
         unsigned char byte = IDisk::ConvertMFMToByte(pattern_byte);
         {
            AddGapRecord(&gap_data, nb_gap_bytes, byte);
            block_descriptor.gap_bits += nb_gap_bytes * 16;
            if (nb_backward_gap_bytes > 0)
            {
               AddGapRecord(&gap_data, nb_backward_gap_bytes, 0x00);
               block_descriptor.gap_bits += nb_backward_gap_bytes * 16;
               imge_item.gap_bits += 16 * nb_backward_gap_bytes;
               imge_item.track_byte += nb_backward_gap_bytes * 2;
               block_descriptor.block_flag |= 2;
            }


            imge_item.gap_bits += 16 * nb_gap_bytes;
            imge_item.track_byte += nb_gap_bytes * 2;
            if (nb_gap_bytes > 0)
               block_descriptor.block_flag |= 1;

            // TODO : Adjust at bit value
            // data itself : Add the data (supposed to pPatternC2 + 16 bits (FC most of the time)
            // From searchOffset -> offsetGap;
            bool byte_size = AddStreamRecord(&data, track_field, search_offset, offset_gap,
                                             disk->side_[side].tracks[tr].size);
            imge_item.data_bits += (offset_gap - search_offset);
            imge_item.track_byte += (offset_gap - search_offset + 7) / 8;

            // Block descriptor : End the description
            block_descriptor.block_bits = (offset_gap - search_offset);
            block_descriptor.sps_specific.cell_type = 1;
            block_descriptor.encoder_type = 1;

            if (!byte_size)
               block_descriptor.block_flag |= 4; // in byte. TODO : Use value for bits
            block_descriptor.gap_value = 0x4E;

            list_block_descriptor.push_back(block_descriptor);
            imge_item.block_count++;

            search_offset = search_next_offset;
         }

         // Next blocks
      }

      delete[]track_field;
   }
   // End of track (or unformatted ones !)
   // TODO
   //End:

   // Copylock ST : the 6th sector of the track is written with ~5% longer cells, all others nominal.
   // The SPS library reproduces it from density type 5 (it slows down block 5), so flag the track
   // when the measured flux timing matches that layout.
   if (!abort && disk->GetPlatform() == IDisk::PLATFORM_ATARI_ST)
   {
      const IDisk::MFMTrack& mfm = disk->side_[side].tracks[tr];
      const int slow = 5 - (starts_with_iam ? 1 : 0); // physical data field that lands in block 5
      if (mfm.nb_sector_density > slow && slow >= 0 && imge_item.block_count > 5)
      {
         bool others_nominal = true;
         for (int i = 0; i < mfm.nb_sector_density; i++)
         {
            if (i != slow && abs(mfm.sector_density[i]) > 20) others_nominal = false;
         }
         if (others_nominal && mfm.sector_density[slow] >= 25 && mfm.sector_density[slow] <= 90)
         {
            imge_item.density = 5; // Copylock ST
         }
      }
   }

   // End : Now compile every descriptor, then every datas ( ).
   // dataoffset and gapoffsets
   memcpy(data_chunk.header.type, "DATA", 4);
   data_chunk.header.length = sizeof (data_chunk) - sizeof (unsigned char*); //+ listBlockDescriptor.size() * sizeof(tBlockDescriptor);
   //dataChunk.header.Length += data.Offset + gapData.Offset;
   data_chunk.size = list_block_descriptor.size() * sizeof(BlockDescriptor);
   int final_gap_offset = data_chunk.size;
   int final_data_offset = final_gap_offset + gap_data.offset;

   data_chunk.size += gap_data.offset;
   data_chunk.size += data.offset;
   data_chunk.bit_size = data_chunk.size * 8;

   // Now compile DATA chunks into a buffer
   data_chunk.buffer = new unsigned char[data_chunk.size];
   int final_offset = 0;

   for (std::vector<BlockDescriptor>::iterator it = list_block_descriptor.begin(); it != list_block_descriptor.end(); it
        ++)
   {
      // Adjust gap offset
      it->sps_specific.gap_offset += final_gap_offset;
      // Adjust data offset
      it->data_offset += final_data_offset;

      // Copy block descriptor
      memcpy(&data_chunk.buffer[final_offset], &(*it), sizeof(BlockDescriptor));
      Swap(&data_chunk.buffer[final_offset], sizeof(BlockDescriptor));
      final_offset += sizeof(BlockDescriptor);
   }
   memcpy(&data_chunk.buffer[final_gap_offset], gap_data.buffer, gap_data.offset);
   memcpy(&data_chunk.buffer[final_data_offset], data.buffer, data.offset);

   data_chunk.data_id = imge_item.data_key = datakey;
   data_chunk.data_crc = CRC::ComputeCrc32(0xEDB88320, data_chunk.buffer, data_chunk.size);

   // Now, update IMGE
   imge_item.track_bits = imge_item.gap_bits + imge_item.data_bits;

   if (side < disk->nb_sides_ && tr < disk->side_[side].nb_tracks)
   {
      if (disk->side_[side].tracks[tr].size != imge_item.track_bits)
      {
         // error ?
         int dbg = 1;
      }
   }
   // .. To add

   // CRC
   imge_item.header.crc = SwapInt(CRC::ComputeCrc32(0xEDB88320, (unsigned char*)&imge_item, sizeof(imge_item)));

   list_imge_.push_back(imge_item);
   list_data_.push_back(data_chunk);

   delete []data.buffer;
   delete []gap_data.buffer;

   return 0;
}


unsigned int CAPSFile::ComputeSize()
{
   unsigned int size = 0;

   size += sizeof (caps_header_);
   size += sizeof (caps_info_);
   size += list_imge_.size() * sizeof(ImgeItem);

   for (std::vector<DataChunks>::iterator it = list_data_.begin(); it != list_data_.end(); it++)
   {
      size += it->header.length;
      size += it->size;
   }

   return size;
}


int CAPSFile::WriteToBuffer(unsigned char* buffer, unsigned int size_of_buffer)
{
   // Write header
   int offset = 0;

   if (size_of_buffer < ComputeSize())
      return -1;

   int size = SwapInt(caps_header_.length);
   memcpy(&buffer[offset], &caps_header_, size);
   offset += size;

   // Write INFO
   size = SwapInt(caps_info_.header.length);
   memcpy(&buffer[offset], &caps_info_, size);
   offset += size;

   // Write IMGE
   for (std::vector<ImgeItem>::iterator it = list_imge_.begin(); it != list_imge_.end(); ++it)
   {
      int base_offset = offset;
      it->header.crc = 0;
      int lg = it->header.length;
      memcpy(&buffer[offset], &(*it), lg);
      Swap(&buffer[offset + 4], lg - 4);
      unsigned int crc = SwapInt(CRC::ComputeCrc32(0xEDB88320, &buffer[base_offset], lg));
      memcpy(&buffer[offset + 8], &crc, 4);
      offset += lg;
   }

   // Write DATA
   for (std::vector<DataChunks>::iterator it = list_data_.begin(); it != list_data_.end(); ++it)
   {
      if (it->header.length >= 4)
      {
         // datablock
         int base_offset = offset;
         it->header.crc = 0;
         int lg = it->header.length;

         memcpy(&buffer[offset], &(*it), sizeof(DataChunks) - sizeof(unsigned char*));
         Swap(&buffer[offset + 4], sizeof(DataChunks) - (4+ sizeof(unsigned char*)));
         offset += sizeof(DataChunks) - sizeof(unsigned char*);

         memcpy(&buffer[offset], it->buffer, it->size);
         offset += it->size;

         unsigned int crc = SwapInt(CRC::ComputeCrc32(0xEDB88320, &buffer[base_offset], lg));
         memcpy(&buffer[base_offset + 8], &crc, 4);
      }
      delete[]it->buffer;
      it->buffer = NULL;
   }

   return 0;
}
