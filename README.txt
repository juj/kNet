   Welcome to KristalliNet!

KristalliNet, kNet for short, is a low-level networking protocol library designed for bit-efficient realtime streaming of custom application-specified messages on top of TCP or UDP. kNet is written in C++.

   Supported Platforms.

kNet has been tested to build on the following platforms:
 - Windows 7 & Visual Studio 2010 Professional
 - Windows 7 & Visual Studio 2008 Standard
 - Ubuntu 9.04 & GCC 4.4.1

   Building kNet.

kNet uses cmake (2.6 or newer) as its build system. On Linux it depends on boost v1.38.0 or newer for threading support. On Windows a CMake flag USE_BOOST can be used to specify whether to depend on boost or not. By default USE_BOOST is on.

Windows:
 - Install cmake. 
 - Optional: Install and build Boost. Edit the root CMakeLists.txt to specify the source directory to boost path.
 - If you do not want to use Boost, edit the root CMakeLists.txt and comment out the USE_BOOST directive. Also, open up include\kNet.h, and remove the #define KNET_USE_BOOST.
 - Optional: Download and install TinyXML. In TinyXML configuration, adjust the CRT runtimes to use the DLL versions. By default, the root CMakeLists.txt looks for TinyXML in a sibling directory 'tinyxml' to kNet root. Change this to the TinyXML path.
 - If you do not want to use TinyXML, edit the root CMakeLists.txt and comment out the USE_TINYXML directive. This will disable the functionality of SerializedMessageList/MessageListParser though. 
 - Execute in project root folder the command 'cmake -G "Visual Studio 10"' (case sensitive!), or click the cmake_vs2010.bat.
 - Open and build the kNet.sln.

Linux:
 - Install Boost libraries and cmake.
 - If you want to use TinyXML, manually specify the TinyXML source directory to root CMakeLists.txt. Otherwise, comment out the USE_TINYXML directive.
 - run 'cmake .' in kNet root folder.
 - run 'make'.

The project output files are placed in the directory kNet/lib.
