# GIF assets

Place source GIF animations for the project in this folder.

Runtime requirement:

- Copy the fallback GIF to the root of the microSD card.
- Rename the fallback exactly to `eye.gif`.
- Create a `/gifs` folder on the microSD card.
- Copy selectable animations into `/gifs`, for example `eye-red.gif`, `eye-green.gif`, `monster.gif`, and `hypnotic.gif`.
- The Arduino sketch restarts the selected GIF automatically when the last frame is reached.
- New GIFs can also be uploaded from the web interface and will be written to `/gifs` on the microSD card.
Recommended format:

- 240 x 240 px
- Animated GIF
- Optimized file size for smooth playback from microSD