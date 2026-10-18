#pragma once 
//////////////////////////////////////////////////////////
// includes.h
//
// standard includes and adapter
//
//
//////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////
// filesystem usage : only for C++17
// NOTE : For c++ <17, try using boost::filesystem (which is roughly the same)

#ifdef _WIN32
#include <filesystem>
#if defined(_MSC_VER) && _MSVC_LANG >= 201703L
namespace fs = std::filesystem;               // current Visual Studio : C++17 filesystem
#else
namespace fs = std::experimental::filesystem; // Visual Studio 2017
#endif
#elif __unix
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif
