   Welcome to KristalliNet!

KristalliNet, kNet for short, is a low-level networking protocol library designed for bit-efficient realtime streaming of custom application-specified messages on top of TCP or UDP. kNet is written in C++.

   Supported Platforms.

kNet has been tested to build on the following platforms:
 - Windows 7 & Visual Studio 2010
 - Ubuntu 9.04 & GCC 4.4.1

   Building kNet.

kNet uses cmake (2.6 or newer) as its build system. On Linux it depends on boost v1.40.0 or newer for threading support. On Windows a CMake flag USE_BOOST can be used to specify whether to depend on boost or not.

Windows:
 - Install cmake. 
 - Optional: Install and build Boost. Edit the CMakeLists.txt and enable the use of USE_BOOST on Windows as well. Edit the source directory to boost path.
 - Execute in project root folder the command 'cmake -G "Visual Studio 10"' (case sensitive!).
 - Open and build the kNet.sln.

Linux:
 - Install Boost libraries and cmake.
 - run 'cmake .' in kNet root folder.
 - run 'make'.

The project output files are placed in the directory kNet/lib.
