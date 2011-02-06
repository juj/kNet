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

# This macro adds to each .cpp file listed in the parameter 'cppFiles' a #define DEBUG_CPP_NAME "thisCppFileName"
#macro(AddCompilationUnitNameDefines cppFiles)
#  message("cppFiles ${cppFiles} ${${cppFiles}}")
#      foreach(srcFile ${cppFiles})
#         get_filename_component(baseName ${srcFile} NAME)
#         set_source_files_properties(${srcFile} PROPERTIES COMPILE_FLAGS "-DDEBUG_CPP_NAME=\"\\\"${baseName}\"\\\"")
#        message("${srcFile} with ${baseName}")
#      endforeach()
#endmacro()

set(USE_BOOST TRUE)
set(BOOST_ROOT "C:/boost_1_44_0")

#set(USE_TINYXML TRUE)
set(TINYXML_ROOT "C:/Projects/3rdParty/tinyxml_2_6_1")

set(USE_QT TRUE)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DTHREAD_CHECKING_ENABLED")

if (USE_BOOST)
   add_definitions(-DKNET_USE_BOOST)

   if (WIN32)
      set(Boost_FIND_REQUIRED TRUE)
      set(Boost_FIND_QUIETLY TRUE)
      set(Boost_DEBUG FALSE)
      set(Boost_USE_MULTITHREADED TRUE)
      set(Boost_DETAILED_FAILURE_MSG FALSE)
      set(Boost_ADDITIONAL_VERSIONS "1.39.0" "1.40.0" "1.41.0" "1.42.0" "1.43.0" "1.44.0")
      set(Boost_USE_STATIC_LIBS TRUE)
   endif()
endif()

if (USE_TINYXML)
   add_definitions(-DKNET_USE_TINYXML)
   
   include_directories(${TINYXML_ROOT})
   link_directories(${TINYXML_ROOT})
   link_directories(${TINYXML_ROOT}/Release)
endif()

file(GLOB kNetSourceFiles ./src/*.cpp)
file(GLOB kNetHeaderFiles ./include/*.h ./include/kNet/*.h ./include/kNet/*.inl)

if (USE_QT)
   add_definitions(-DKNET_USE_QT)
   
   set(QT_USE_QTMAIN TRUE)
   set(QT_USE_QTUITOOLS TRUE)
   find_package(Qt4 REQUIRED)
   
   file(GLOB kNetMocHeaderFiles ./include/kNet/qt/*.h)
   file(GLOB kNetQtSourceFiles ./src/qt/*.cpp)
   file(GLOB kNetQtUiFiles ./ui/*.ui)
   QT4_WRAP_CPP(kNetMocSrcFiles ${kNetMocHeaderFiles})
   set(QT4_UI_OUTPUT_DIR ./include/kNet/qt/ui)
   
   MACRO(QT4_WRAP_UI_CUSTOM_OUTPUT outfiles)
     QT4_EXTRACT_OPTIONS(ui_files ui_options ${ARGN})
     FOREACH(it ${ui_files})
       GET_FILENAME_COMPONENT(outfile ${it} NAME_WE)
       GET_FILENAME_COMPONENT(infile ${it} ABSOLUTE)
       SET(outfile ${QT4_UI_OUTPUT_DIR}/ui_${outfile}.h) # Here we set output
       GET_FILENAME_COMPONENT(outfile ${outfile} ABSOLUTE)
#             message(${outfile})
#       message("in")
#       message(${infile})
       ADD_CUSTOM_COMMAND(OUTPUT ${outfile}
         COMMAND ${QT_UIC_EXECUTABLE}
         ARGS ${ui_options} -o ${outfile} ${infile}
         MAIN_DEPENDENCY ${infile})
       SET(${outfiles} ${${outfiles}} ${outfile})
     ENDFOREACH()    
   ENDMACRO()   
   
   include_directories(${QT4_UI_OUTPUT_DIR})
  QT4_WRAP_UI_CUSTOM_OUTPUT(kNetUiHeaderFiles ${kNetQtUiFiles})
#  message(${kNetUiHeaderFiles})
   set(kNetSourceFiles ${kNetSourceFiles} ${kNetMocSrcFiles} ${kNetQtSourceFiles})
#   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetMocHeaderFiles})
   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetMocHeaderFiles} ${kNetUiHeaderFiles})
   include(${QT_USE_FILE})
   set(kNetLinkLibraries ${kNetLinkLibraries} ${QT_LIBRARIES})
endif()

if (USE_BOOST)
   file(GLOB kNetBoostSourceFiles ./src/boost/*.cpp)
   set(kNetSourceFiles ${kNetSourceFiles} ${kNetBoostSourceFiles})
endif()

if (WIN32)
   file(GLOB kNetWin32SourceFiles ./src/win32/*.cpp)
   file(GLOB kNetWin32HeaderFiles ./include/kNet/win32/*.h)

   if (USE_BOOST)
      list(REMOVE_ITEM kNetWin32SourceFiles ${PROJECT_SOURCE_DIR}/./src/win32/W32Thread.cpp)
   endif()

   set(kNetSourceFiles ${kNetSourceFiles} ${kNetWin32SourceFiles})
   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetWin32HeaderFiles})

   add_definitions(-D_WINSOCKAPI_)
   add_definitions(-D_CRT_SECURE_NO_WARNINGS)
   add_definitions(-DKNET_MEMORY_LEAK_CHECK)
   
   set(kNetLinkLibraries ${kNetLinkLibraries} ws2_32.lib)
   
elseif (UNIX)
   file(GLOB kNetUnixSourceFiles ./src/unix/*.cpp)
   file(GLOB kNetUnixHeaderFiles ./include/*.h ./include/kNet/*.h ./include/kNet/unix/*.h)

   set(kNetSourceFiles ${kNetSourceFiles} ${kNetUnixSourceFiles})
   set(kNetHeaderFiles ${kNetHeaderFiles} ${kNetUnixHeaderFiles})

   add_definitions(-DLINUX)
endif()

#AddCompilationUnitNameDefines(kNetSourceFiles)
#TODO: To clean up, move the lines of code below into a macro, like shown above. Disabled for now since passing a list to a CMake macro was problematic.
foreach(srcFile ${kNetSourceFiles})
   get_filename_component(baseName ${srcFile} NAME)
   set_source_files_properties(${srcFile} PROPERTIES COMPILE_FLAGS "-DDEBUG_CPP_NAME=\"\\\"${baseName}\"\\\"")
endforeach()

add_library(kNet STATIC ${kNetSourceFiles} ${kNetHeaderFiles})

if (USE_TINYXML)
   target_link_libraries(kNet debug tinyxmld.lib optimized tinyxml.lib)
endif()

# Add the main kNet include directory root folder to all projects.
include_directories(./include)

if (USE_BOOST)
   find_package(Boost 1.38.0 COMPONENTS thread)
   if (Boost_FOUND)
      include_directories(${Boost_INCLUDE_DIRS})
      link_directories(${Boost_LIBRARY_DIRS})
   else()
      message(FATAL_ERROR "Boost not found!")
   endif()
   
   set(kNetLinkLibraries ${kNetLinkLibraries} ${Boost_LIBRARIES})   
endif()

target_link_libraries(kNet ${kNetLinkLibraries})

#add_subdirectory(samples/ConnectFlood)
add_subdirectory(samples/FileTransfer)
#add_subdirectory(samples/FirewallTest)
#add_subdirectory(samples/HelloClient)
#add_subdirectory(samples/HelloServer)
#add_subdirectory(samples/LatencyTest)
#add_subdirectory(samples/MessageCompiler)
#add_subdirectory(samples/SilenceTest)
#add_subdirectory(samples/SimpleChat)
#add_subdirectory(samples/SpeedTest)
#add_subdirectory(samples/TrashTalk)

#add_subdirectory(tests)

#set_target_properties(kNet FileTransfer PROPERTIES 
#set_target_properties(kNet Tests ConnectFlood FileTransfer FirewallTest HelloClient HelloServer LatencyTest MessageCompiler SilenceTest SimpleChat SpeedTest TrashTalk PROPERTIES 

set_target_properties(kNet FileTransfer PROPERTIES 
	ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
	LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
