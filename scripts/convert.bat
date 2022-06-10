echo usage: convert srcdir dstdir
@echo off

if not exist %2 mkdir %2

set exedir=%cd%
pushd %2
set outdir=%cd%
popd
pushd %1

for /R %%f in (*.dds) do (
	echo %%~nf.dds
	%exedir%\DdsToXet.exe -in %%~nf.dds -out %outdir%\%%~nf.xet %3
)
popd
