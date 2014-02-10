@rem To explicitly configure the location of Qt, uncomment and specify the following lines.
@rem SET QMAKESPEC=E:\Qt\2010.02.1\qt\mkspecs\win32-msvc2008
@rem SET QTDIR=E:\Qt\2010.02.1\qt
cmake -G "Visual Studio 10" %*
pause