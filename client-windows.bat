mkdir output
mkdir output\mods
mkdir output\mods\deathmatch
mkdir output\server
mkdir output\server\mods
mkdir output\server\mods\deathmatch

::utility\wget https://openvice.net/bases/openvice-base-1.zip
utility\unzip openvice-base-1.zip -d output

copy "release\Multi Theft Auto.exe" "output\OpenVice.exe"
copy "release\MTA\*.dll" "output\MTA"
copy "release\mods\deathmatch\*.dll" "output\mods\deathmatch"
copy "release\server\*" "output\server"
copy "release\server\mods\deathmatch\*" "output\server\mods\deathmatch"