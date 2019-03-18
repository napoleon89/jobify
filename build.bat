@echo off
set compiler_options=-MT -D_ITERATOR_DEBUG_LEVEL=0 -D_CRT_SECURE_NO_WARNINGS -W4 -GT -FC -WX -Oi -Od -Gm- -GR- -Z7 -nologo -wd4312 -wd4806 -wd4701 -wd4505 -wd4201 -EHsc -wd4100 -wd4127 -wd4189 -wd4244 -wd4005 -Fo:..\temp\ -I..\deps\include -I..\src
set linker_options=-incremental:no -opt:ref -LIBPATH:../deps/lib/x64/windows/ 

if not exist build mkdir build
if not exist temp mkdir temp

pushd build
del *.pdb > NUL 2> NUL


cl %compiler_options% -Fe:jobify.exe -MP ../src/main.cpp  -Fm:jobify.map /link %linker_options% user32.lib kernel32.lib -SUBSYSTEM:CONSOLE 

popd