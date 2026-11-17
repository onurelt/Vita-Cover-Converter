#pragma once
/* Extracts embedded JPEG or PNG from ID3v2.2, v2.3 and v2.4 tags.
 * Return 0 on success, -2 if no supported artwork, -1 on I/O/invalid data.
 * is_png is set to 1 for PNG, 0 for JPEG. */
int vcc_extract_artwork(const char *mp3, const char *output, int *is_png);
/* Convert an embedded PNG to a letterboxed 256x256 cover.jpg. */
int vcc_png_to_jpeg(const char *input, const char *output);
