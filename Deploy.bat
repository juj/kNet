echo off
echo This script deploys kNet client header and lib files
echo to the directory specified in the first argument
echo to this script: %1

IF (%1)==() GOTO INSTRUCTIONS
echo Press any key to start
pause

mkdir %1
mkdir %1\include
mkdir %1\include\kNet
mkdir %1\include\kNet\qt
mkdir %1\include\kNet\qt\ui
mkdir %1\include\kNet\win32

copy include\*.* %1\include\
copy include\kNet\*.* %1\include\kNet\
copy include\kNet\qt\*.* %1\include\kNet\qt\
copy include\kNet\qt\ui\*.* %1\include\kNet\qt\ui\
copy include\kNet\win32\*.* %1\include\kNet\win32\

mkdir %1\lib
mkdir %1\lib\Debug
mkdir %1\lib\MinSizeRel
mkdir %1\lib\Release
mkdir %1\lib\RelWithDebInfo

copy lib\Debug\kNet.lib          %1\lib\Debug
copy lib\Debug\kNet.pdb          %1\lib\Debug
copy lib\MinSizeRel\kNet.lib     %1\lib\MinSizeRel
copy lib\Release\kNet.lib        %1\lib\Release
copy lib\RelWithDebInfo\kNet.lib %1\lib\RelWithDebInfo
copy lib\RelWithDebInfo\kNet.pdb %1\lib\RelWithDebInfo

echo Done.
GOTO END

:INSTRUCTIONS
echo To use this script, pass in as the first parameter
echo the target directory to deploy kNet to.
:END
pause
