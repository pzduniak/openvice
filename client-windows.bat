mkdir output

utility\wget https://openvice.net/bases/openvice-base-1.zip
utility\unzip openvice-base-1.zip -d output

copy "release\Multi Theft Auto.exe" "output\OpenVice.exe"
copy "release\MTA\*.dll" "output\MTA"
copy "release\mods\deathmatch\*.dll" "output\mods\deathmatch"
copy "release\server" "output\server"