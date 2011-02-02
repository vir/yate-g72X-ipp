src3="g723codec.cpp decg723.c encg723.c owng723.c vadg723.c aux_tbls.c"
src9="g729codec.cpp decg729fp.c encg729fp.c owng729fp.c vadg729fp.c"

cc=gcc
o="-O3 -fomit-frame-pointer"
#inc=/home/arkadi/opt/yate2/include/yate
inc=/home/arkadi/opt/yate21/include/yate
#inc=/home/arkadi/opt/yate-svn/include/yate
# uncomment if you have Yate 2.1 or SVN r2745 or later
r2745=-DYATE_G72X_POST_R2745

function build()
{
    "$cc" -shared -Xlinker -x -o $1 \
        $r2745 -I$inc -I"$ipproot"/include $ippstatic_include \
        $opt $o \
        -fPIC -fno-check-new -fno-exceptions \
        $src \
        -L"$ipproot"/lib $ipplibs
}

# 32-bit
opt=-march=pentium4
ipproot=/opt/intel/ipp/6.0/ia32
ipplibs="-lippscmerged -lippsrmerged -lippsmerged -lippcore"
ippcore=w7 # pentium4 sse2
#ipproot=/opt/intel/ipp/5.3/ia32
#ippcore=a6 # pentium3 sse
ippstatic_include="-include $ipproot/tools/staticlib/ipp_$ippcore.h"
src="$src3" build g723.yate
src="$src9" build g729.yate

# 64-bit
cc=x86_64-unknown-linux-gnu-gcc-4.3.2
opt=-march=nocona
ipproot=/opt/intel/ipp/6.0/em64t
ipplibs="-lippscmergedem64t -lippsrmergedem64t -lippsmergedem64t -lippcoreem64t"
ippcore=m7 # pentium4 sse3 em64t
ippstatic_include="-include $ipproot/tools/staticlib/ipp_$ippcore.h"
src="$src3" build g723_x64.yate
src="$src9" build g729_x64.yate

# IPP cores are:
# 32-bit
# px - pentium mmx
# a6 - pentium3 sse (removed in IPP 6.0)
# w7 - pentium4 sse2
# t7 - pentium4 prescott sse3
# v8 - core2 ssse3
# p8 - core2 penryn, core i7 nehalem sse4.1
# s8 - atom
# 64-bit
# mx - older amd64 w/o sse3?
# m7 - pentium4 sse3 em64t
# u8 - core2 ssse3
# y8 - core2 penryn, core i7 nehalem sse4.1
# n8 - atom
