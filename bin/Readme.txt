convert.exe
is from ImageMagick 7.1.0-57 package


RFA - PACK

NEW! "-Compress" option enables compression


rfaPack.exe [folder to pack] [baseFolderName] [archive.rfa]
	rfaPack.exe d:\contentForMyMod\menu menu "c:\bf 1942\mods\myMod\Archives\menu.rfa"


ProgramName [sourceDir] [PackDirName] [Archive.rfa] [ -u update existing .rfa | -Compress]
   RfaPack.exe d:/menu menu menu.rfa
   RfaPack.exe d:/menu menu menu.rfa -u
   RfaPack.exe d:/menu menu menu.rfa -Compress
   RfaPack.exe d:/menu menu menu.rfa -u -Compress

RFA - UNPACK

rfaUnpack.exe [archive.rfa] [extractToPath]
	rfaUnpack.exe objects.rfa c:\myExtractedFiles