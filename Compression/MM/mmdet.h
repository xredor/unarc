// MM type detection library

// WAV header recognition
int autodetect_wav_header (void *buf, long size, int *is_float, int *num_chan, int *word_size, int *offset);
// Call to MM detector. channels[] - variants of number of channels it should try
int autodetect_by_entropy (void *buf, int bufsize, int channels[], int bitvalues[], double min_entropy, int *is_float, int *num_chan, int *word_size, int *offset);
// How many bytes to read to check for MM
int  detect_mm_bytes (int mode, int filesize);
// Return 1 if buffer given contain MM data
int  detect_mm (int mode, void *buf, int bufsize);
// Check whether buffer contains header of MM file
int detect_mm_header (int mode, void *buf, int bufsize);
// Returns 'type' describing data in the buffer ($compressed/$text/$binary)
void detect_datatype (BYTE *buf, int bufsize, char *type);


