# Copyright 2010 Jukka Jylänki

#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at

#      http://www.apache.org/licenses/LICENSE-2.0

#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

cmake_minimum_required(VERSION 2.6)
project(kNet)

if (UNIX) # By default only depend on Boost on Linux. You can manually remove this to also use Boost on windows.
set(USE_BOOST TRUE)
endif()

if(USE_BOOST)
   add_definitions(-DKNET_USE_BOOST)

   if (WIN32)
      set(Boost_USE_MULTITHREADED TRUE)
      set(Boost_USE_STATIC_LIBS FALSE)
      set(Boost_FIND_REQUIRED TRUE)
      set(Boost_FIND_QUIETLY TRUE)
      set(Boost_DEBUG FALSE)
      set(Boost_DETAILED_FAILURE_MSG FALSE)
      set(Boost_USE_MULTITHREADED TRUE)
      set(Boost_USE_STATIC_LIBS TRUE)
      set(Boost_ADDITIONAL_VERSIONS "1.39.0" "1.40.0" "1.41.0" "1.42.0" "1.43.0" "1.44.0")

      set(BOOST_ROOT "C:/boost_1_44_0")
      set(ENV{BOOST_ROOT} "C:/boost_1_44_0")
   endif()
endif()

file(GLOB kNetSourceFiles ./src/*.cpp)
file(GLOB kNetHeaderFiles ./include/*.h ./include/kNet/*.h ./include/kNet/*.inl)

if(USE_BOOST)
   file(GLOB kNetBoostSourceFiles ./src/boost/*.cpp)
   set(kNetSourceFiles ${kNetSourceFiles} ${kNetBoostSourceFiles})
endif()

if(WIN32)
   file(GLOB kNetWin32SourceFiles ./src/win32/*.cpp)
   file(GLOB kNetWin32HeaderFiles ./include/kNet/win32/*.h)

   if(USE_BOOST)
      list(REMOVE_ITEM kNetWin32SourceFiles ${PROJECT_SOURCE_DIR}/./src/win32/W32Thread.cpp)
   endif()

   set(kNetSourceFiles ${kNetSourceFiles} ${kNetWin32SourceFiles})
   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetWin32HeaderFiles})

   add_definitions(-D_WINSOCKAPI_)
   add_definitions(-D_CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
   file(GLOB kNetUnixSourceFiles ./src/unix/*.cpp)
   file(GLOB kNetUnixHeaderFiles ./include/*.h ./include/kNet/*.h ./include/kNet/unix/*.h)

   set(kNetSourceFiles ${kNetSourceFiles} ${kNetUnixSourceFiles})
   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetUnixHeaderFiles})

   add_definitions(-DLINUX)
endif()

add_library(kNet STATIC ${kNetSourceFiles} ${kNetHeaderFiles})

include_directories(./include)

if (USE_BOOST)
   find_package(Boost 1.40.0 COMPONENTS thread)
   if(Boost_FOUND)
      include_directories(${Boost_INCLUDE_DIRS})
      link_directories(${BOOST_LIBRARYDIR})
   else()
      message(FATAL_ERROR "Boost not found!")
   endif()
endif()

if(WIN32)
   target_link_libraries(kNet ws2_32.lib ${Boost_LIBRARIES})
endif()

add_subdirectory(samples/FileTransfer)
add_subdirectory(samples/HelloServer)
add_subdirectory(samples/HelloClient)
add_subdirectory(samples/SimpleChat)

#add_subdirectory(samples/TrashTalk)
#add_subdirectory(samples/SpeedTest)
#add_subdirectory(samples/LatencyTest)
#add_subdirectory(samples/FirewallTest)
#add_subdirectory(samples/ConnectFlood)
#add_subdirectory(samples/SpeexVoipServer)

if (WIN32)
#add_subdirectory(samples/RawTransfer)

#add_subdirectory(samples/SpeexVoip)

#set_target_properties(SpeexVoip SimpleChat PROPERTIES 

#set_target_properties(SimpleChat PROPERTIES 
#	ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
#	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
#	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

endif()

#set_target_properties(kNet FileTransfer HelloServer HelloClient TrashTalk SpeedTest LatencyTest FirewallTest ConnectFlood SpeexVoipServer PROPERTIES 

set_target_properties(kNet FileTransfer HelloServer HelloClient SimpleChat PROPERTIES 
	ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
