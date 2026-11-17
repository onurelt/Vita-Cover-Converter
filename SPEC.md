# Vita Cover Converter

Native PS Vita utility for preparing MP3 music folders for Sleep Mixer.

## Confirmed workflow
1. Browse MP3 files, starting at `ux0:data/VitaSend/received/`.
2. Read embedded ID3 album artwork from the selected MP3; report clearly if none exists.
3. Convert artwork into a Sleep Mixer-compatible image, without automatic cropping.
4. Preview `ux0:data/SleepMixer/Music/`, including existing Track folders, and automatically choose the next unused `Track####` number.
5. Show the complete destination and request confirmation.
6. Copy the MP3 and converted artwork into the new folder. Preserve the original MP3 filename; do not ask to rename the song.

## Safety requirements
- Never overwrite an existing Track folder.
- Do not delete or alter source MP3 files.
- Use temporary output files and handle failed or interrupted copies.
- Do not modify the Sleep Mixer application.
- Validate artwork dimensions, file sizes, and ID3 versions before decoding.
- Keep the interface minimal and controller-friendly.

## Status
Repository initialized. Native implementation and VPK build are pending.
