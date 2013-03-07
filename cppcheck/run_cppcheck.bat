:: The warning unusedFunction: 'The function 'funcName' is never used.' is suppressed because as a generic utility library, most of the functions are not used by the library itself.

"C:\Program Files (x86)\Cppcheck\cppcheck" --version

"C:\Program Files (x86)\Cppcheck\cppcheck" --template "{file}({line}): ({severity}) ({id}): {message}" -UMATH_QT_INTEROP -I../src -I../include -rp=../src --enable=all --suppress=unusedFunction --error-exitcode=1 --force --suppressions suppressions.txt ../src

pause

