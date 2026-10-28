
# SugarConvDsk

[![Build Status](https://travis-ci.com/Tom1975/SugarConvDsk.svg?branch=master)](https://travis-ci.com/Tom1975/SugarConvDsk)
[![Build status](https://ci.appveyor.com/api/projects/status/oq746hdj4wrn8rod?svg=true)](https://ci.appveyor.com/project/Tom1975/sugarconvdsk)

Convert any Amstrad CPC or Atari ST dump file format to other format

Usage
=====
Usage : SugarConvDsk source [destination] [-s=side] [-second=path][-o=outputformat] [-r] [-f=filter]
    or  SugarConvDsk -cat=user [-sort] [-l] [-c] source 

	If -cat option is not used, SugarConvDsk will perform a format conversion.

    -s=side : Select side of the disk to convert.
       Side can be 1 or 2
       If omitted, both side are written (if relevant for the format)
    
    -second=path : the 2nd side is replaced by the dump source specified in 'path'

    -ipfraw : Write IPF tracks with the original, larger encoding (MFM cells instead of decoded bytes).

    -hfev1 : Write HFE files in the original HFE format (HXCPICFE) instead of HFE v3.

    -p=platform : Target machine recorded in the output (IPF platform field) : ST or CPC.
       If omitted, it is guessed from the dump : more than 60 cylinders means Atari ST.

    -o=outputformat : Select output format. Can take the following values:
        EDSK : Extended Dsk format
        HFE : HFE format
        IPF : IPF format
        SCP : Supercard Pro format
    If this parameter is not used, default output format is EDSK

    source : The source file can be in the following format :
        CTRAW : CT-RAW format
        DSK : Dsk format
        EDSK : Extended Dsk format
        HFE : HFE format
        IPF : IPF format
        SCP : Supercard Pro format
        KRYOFLUX : Kryoflux RAW file format
		
        If the source file is a directory : In this case, every files in the given directory are converted to the 'output' directory

    destination : output file. If source file is a directory, destination is used as an output directory

    -r : If the source file is a directory, convert recursively the given directory.
    -f=filter : If the source file is a directory, set a filter for the files to convert.
	
	Example : SugarConvDsk d:\dump d:\result -o=IPF -r -f=[CPC]*.dsk 
	This will convert to IPF every file of the form "[CPC]*.dsk", from directory d:\result, and subdirectories.
	Result files will be saved in the d:\result directory.
	

	If -cat option is used, SugarConvDsk will display the content of the diskn, and will not do a convertion.
	In this case, destination and conversion flags are ignored.

    -cat=user : This option will print the disk content, based on the USER specified. If ALLUSERS is used, all users will be displayed.
    	If used, this option will prevent a convertion.
	-sort : This flag will wort the files in alphabetical order.
	-l : Add flags H for hidden files, and R for read-only files. List isdisplayed with one file per line.
	-c : Display file on the same line, separated with semicolumn
    

This tool mainly convert any format into an internal format, then write it back to the specified one.
Internal format is bases on MFM, with some specific attributes for every bit, which can be weak or optional.
The internal format is also not revolution-dependant : If a dump use more than one revolution, to keep track of weak sector for example, a specific algorithm will rebuild a single track from these revolutions, adding the attributes needed to describe everything. Also, an error correction is done, to avoid being fooled by reading error.

Atari ST
========
Flux dumps (SCP, KryoFlux RAW stream sets, CT-RAW) of Atari ST disks can be converted to IPF :

	SugarConvDsk "Dungeon Master.scp" -o=IPF
	SugarConvDsk dump/track00.0.raw -o=IPF -p=ST

 - 80 to 84 cylinders, single or double sided. For SCP, the sides come from the header's heads byte,
   and both the interleaved (cylinder*2+head) and sequential single-sided track tables are handled.
 - 9, 10 and 11 sector formats, long tracks.
 - IPF output records the Atari ST platform and the real number of sides.
 - Copylock ST : the cell timing of each sector is measured from the flux. When the 6th sector is
   ~5% slower than the others, the track is written with the IPF "Copylock ST" density so emulators
   using the SPS library reproduce it. Set SUGARCONV_DEBUG_DENSITY=1 to print per-sector timings.
 - Speedlock-style density keys (a slow patch followed by a fast patch inside one sector, e.g. Colorado
   track 1) : the timing is measured inside the sector. IPF output starts the track at that sector and
   cuts it into blocks at the patch edges. IPF density type 7 is used by default (block 1 +5%, as measured
   on the original disks, needed by Dragon's Breath) ; -speedlock10 selects type 6 (+10% / -10%, as in some
   existing images). HFE v3 and SCP output play the patches at their measured speed.
 - Custom data-stream tracks ("Amiga tracks", MacroDOS : one sync mark then the whole track as one MFM
   stream, no sector headers, e.g. Dragon's Breath) : merged across revolutions on their sync, written as
   the sync plus decoded bytes, with the unstable write splice before the sync kept as fuzzy data.
   The index is placed where it is on the median revolution (the index jitters by a few cells and the
   write splice sits on it; the first revolution of a dump is often the extreme one, which stops Hot Rod
   from loading). SUGARCONV_STREAM_INDEX_REV0=1 keeps the index of revolution 0.
 - Copylock ST is detected from SCP, KryoFlux and CT-RAW (CT-RAW uses its own density map).
 - Fuzzy sectors (Dungeon Master...) : every cell of a data field that is not stable across the dumped
   revolutions is written as fuzzy, so each read returns different data with a bad CRC.
 - Unstable cells around the write splice (in the gap after the last sector) are dump noise and are
   written as plain gap, matching SPS images.
 Checked against real dumps : Defender II CT-RAW gives sector data, Copylock ST density and weak areas
 identical to the SPS IPF ; King's Quest 3 (3 disks) KryoFlux and CT-RAW give identical IPFs.

IPF output stores sectors the way SPS images do : ID and data fields as decoded bytes, fuzzy bytes as
fuzzy elements, and gaps as a fill byte plus the 00 bytes before the next sync, which the reader regenerates
(write splices in gaps are smoothed out, as in SPS images). Blocks that can't be represented exactly this way
(non-standard MFM, odd sector sizes, irregular gaps) keep the exact cell-level encoding. Files are about half
the size they were : Defender II is 1,018,552 bytes against 1,017,811 for the SPS image, with identical content.
-ipfraw restores the previous encoding.

HFE output is HFE v3 (HXCHFEV3) by default, as read by FlashFloppy and the HxC tools :
 - fuzzy / weak bits are written as random-byte opcodes, so a fuzzy sector reads differently each time ;
 - sectors measured slower or faster (Copylock ST) get their own bit rate ;
 - Atari ST disks are flagged with the Atari ST interface mode.
Use -hfev1 for the original format if your emulator or firmware doesn't read v3 (weak bits and
bit rate changes are then lost). HFE v3 files can also be read back as a source.

SCP output (-o=SCP) follows the SuperCard Pro image specification v2.5 (Tests/AtariST/scpcheck.py
validates a file against it). It writes a real flux image, e.g. to write a disk back with a Greaseweazle
or SuperCard Pro, or for emulators that read SCP :
 - disk type Atari ST SS/DS (0x14/0x15) or Amstrad CPC (0x70), heads byte (both / side 0 only, single
   sided images use the even table entries only), 168-entry track table, 200 ms revolutions ;
 - FLAGS : index-cued, normalised flux (generated from decoded cells), 3rd-party flux creator ;
 - every cell gets its measured length, so Copylock ST and Speedlock-style keys keep their timing ;
 - disks with weak bits get 3 revolutions : fuzzy areas take new random (valid MFM) data in each, and
   phase-slip areas (Time disk 2 hidden sectors) gain or lose a cell between revolutions, as on the disk ;
 - unformatted tracks are written as noise ;
 - -scpnoflux writes weak / fuzzy areas instead as gaps without flux transitions, in a single revolution :
   the way weak bits are duplicated on a real disk (the drive reads noise there). Use it to write the
   SCP back to a floppy with a Greaseweazle or SuperCard Pro. Nearby weak spots are joined and every gap
   is at least 8 cells long. When such an image is read back (FLAGS "normalised" set), gaps of 8 cells
   or more inside a formatted track are flagged weak again (SUGARCONV_SCP_NO_GAP_WEAK=1 disables it).
The previous CPC SCP writer (disk type 0 = C64, heads byte "side 1 only", side 0 repeated) is kept
behind SUGARCONV_SCP_LEGACY=1.
SCP sources : the resolution (byte 0x0B), 8-bit cells, the extended-mode table position and the older
166-entry tables are handled. Atari ST disks are decoded as one continuous flux stream cut at the index
pulses (sectors running across the index stay intact) ; SUGARCONV_SCP_PER_REVOLUTION=1 restores the old
decoding.
IPF sources with density protections (Copylock ST, Speedlock) are given back their timing when read, so
HFE and SCP made from an IPF keep the protection.

Tests/AtariST contains a flux generator for synthetic ST disks and an IPF checker based on the SPS
capsimage library, hfecheck.py which decodes HFE v1/v3 the way FlashFloppy does, and run_tests.sh.
ipfconsist.py checks that an IPF's block and track sizes add up the way strict readers expect (Aufit
reports "IPF File Error: inconsistent track content" otherwise) : track bytes = each block rounded up to
whole bytes, and weak data in bit-sized blocks counted in data bits (2 cells each). IPFs written by
SugarConvDsk before commit 48daf8a can break both rules ; ipfrepair.py IN OUT corrects them without
converting again.

Compilation
=
This tool can be compiled on various system.
Known working targets are :
 - Windows : Both x64 and x86
 - Ubuntu 

The tool use CMake as makefile generator, and zLib as an external library.
Also, a modern compiler is required as it use some C++17 features (through experimental/filesystem for gcc7.2 and Visual 2017 15.6).

The code is a result of various and extended experimentation, so it may looks like a bit messy (as I tried to make it works before making it right)
