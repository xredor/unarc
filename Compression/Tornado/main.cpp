// (c) Bulat Ziganshin <Bulat.Ziganshin@gmail.com>
// (c) Joachim Henke
// Tornado - fast LZ77-based compression algorithm.
// This module is a command-line driver to Tornado library.
#define FREEARC_STANDALONE_TORNADO
#include "Tornado.cpp"
#include "../Common.cpp"

static const char *PROGRAM_NAME = "Tornado";

// Extensions of compressed and decompressed files
static const char *COMPRESS_EXT = ".tor", *DECOMPRESS_EXT = ".untor";

// Codec and parser names for humans
static const char *codec_name[]  = {"storing", "bytecoder", "bitcoder", "hufcoder", "aricoder"};
static const char *parser_name[] = {"", "greedy", "lazy", "flexible", "optimal"};

// Returns human-readable method description
static char *name (PackMethod method)
{
    static char namebuf[200], h[100], b[100], auxhash_size[100], u[100], ah[100];
    const char*hashname[] = {"hash4", "chash4", "cchash4", "", "", "cchash5", "cchash6", "cchash7"};
    int c  = method.encoding_method;
    int l  = method.hash_row_width;
    showMem (method.hashsize,     h);
    showMem (method.buffer,       b);
    showMem (method.auxhash_size, auxhash_size);
    int x  = method.caching_finder;
    int p  = method.match_parser;
    int h3 = method.hash3;
    sprintf (u, x<2 && method.update_step<999? "/u%d":"", method.update_step);
    sprintf (ah, x>4? " + %s:%d %s":"", auxhash_size, method.auxhash_row_width, x>5? "cchash4" : "exhash4");
    sprintf (namebuf, c==STORING? codec_name[c] : "%s parser, %s:%d%s %s%s%s, buffer %s, %s%s",
             parser_name[p], h, l, u, hashname[x], ah, h3==2?" + 256kb hash3 + 16kb hash2":h3?" + 16kb hash3 + 4kb hash2":"", b, codec_name[c], method.find_tables? "" : " w/o tables");
    return namebuf;
}

enum OPMODE {AUTO, COMPRESS, DECOMPRESS, BENCHMARK, HELP};

// Structure for recording compression statistics and zero record of this type
struct Results {
  OPMODE mode;                 // Operation mode
  PackMethod method;           // Compression method used
  char method_name[100];       // Short name of compression method
  char *filename;              // Names of input/output files
  char outname[MY_FILENAME_MAX];
  FILE *fin, *fout;            // Input and output files
  FILESIZE filesize;           // Size of input file
  FILESIZE insize, outsize;    // How many bytes was already read/written
  FILESIZE qoutsize;           // Size of compressed output (including data not yet written to disk)
  double start_time;           // When (de)compression was started
  double lasttime, lasttime2;  // Last time when we've updated progress indicator/console title
  bool   use_cpu_time;         // Compute pure CPU time used (instead of wall-clock time)
  bool   show_exact_percent;   // Show xx.x% progress indicator instead of xx%
  bool   quiet_title, quiet_header, quiet_progress, quiet_result;
                               // Don't show window title/compression header/progress/results
};

// Return current time (cpu time used or wall clock time). Return 0 if we disabled timing at compile time
#ifdef FREEARC_NO_TIMING
#define GetSomeTime() 0
#else
#define GetSomeTime() (r.use_cpu_time? GetThreadCPUTime() : GetGlobalTime())
#endif

// Callback function called by compression routine to read/write data.
// Also it's called by the driver to init/shutdown its processing
int ReadWriteCallback (const char *what, void *buf, int size, void *r_)
{
  Results &r = *(Results*)r_;        // Accumulator for compression statistics

  if (strequ(what,"init")) {
    r.insize = r.outsize = r.qoutsize = 0;
    r.show_exact_percent = FALSE;
    r.start_time = r.lasttime = r.lasttime2 = GetSomeTime();
#ifdef FREEARC_WIN
    if (strequ (r.filename, "-"))   // On windows, get_flen cannot return real filesize in situations like "type file|tor"
        r.filesize = -1;
    else
#endif
        r.filesize = get_flen(r.fin);
    sprintf(r.method_name, r.mode==COMPRESS? "-%d: " : "", r.method.number);
    return FREEARC_OK;

  } else if (strequ(what,"read")) {
    //double before = GetSomeTime();
    int n = file_read (r.fin, buf, size);
    r.insize += n;
    //r.start_time -= GetSomeTime()-before;   // Don't take into account I/O times
    return n;

  } else if (strequ(what,"write") || strequ(what,"quasiwrite")) {
    //double before = GetSomeTime();
    if (strequ(what,"write")) {
      if (r.fout)
      {
          if (size != file_write (r.fout, buf, size))
              return FREEARC_ERRCODE_WRITE;
      }
      r.outsize += size;
    } else {
      r.qoutsize += size;
    }
#ifndef FREEARC_NO_TIMING
    double after = GetSomeTime();
    //r.start_time -= after-before;   // Don't take into account I/O times

    // Update progress indicator every 0.1 seconds
    if (!r.quiet_progress && r.insize && mymax(r.outsize,r.qoutsize) && after > r.lasttime+0.1)
    {
      double time = after - r.start_time;      // Time used so far
      char percents0[100] = "",  remains0[100] = "", percents[100] = "",  remains[100] = "";
      if (r.filesize) {
        // If 1% progress requires more than 1-2 seconds - display xx.x%, otherwise xx%
        // (we don't want to switch it too frequently, therefore "?1:2")
        r.show_exact_percent  =  double(r.filesize)/r.insize*time > (r.show_exact_percent?1:2)*100;
        if (r.show_exact_percent)
             sprintf (percents0, "%.1lf%%", double(int(double(r.insize)*100/r.filesize*10))/10);
        else sprintf (percents0, "%d%%", int(double(r.insize)*100/r.filesize));
        sprintf (percents, "%s: ", percents0);

        int remain = int(double(r.filesize-r.insize)/r.insize*time)+1;
        if (remain>=3600)
             sprintf (remains0, "%02d:%02d:%02d", remain / 3600, (remain % 3600) / 60, remain % 60);
        else sprintf (remains0, "%02d:%02d", remain / 60, remain % 60);
        sprintf (remains, ". Remains %s", remains0);
      }
      double insizeMB  = double(r.insize)/1000/1000;
      double outsizeMB = double(mymax(r.outsize,r.qoutsize))/1000/1000;
      double ratio     = (r.mode==COMPRESS? outsizeMB/insizeMB : insizeMB/outsizeMB) * 100;
      double speed     = (r.mode==COMPRESS? insizeMB : outsizeMB) / mymax(time,0.001);
      if (!r.quiet_result && !strequ (r.outname, "-"))
        fprintf (stderr, "\r%s%s%.3lf -> %.3lf mb (%.1lf%%), speed %.3lf mb/sec%s   ",
                  r.method_name, percents, insizeMB, outsizeMB, ratio, speed, remains);

      if (!r.quiet_title && r.filesize && after > r.lasttime2+0.5) {  // Update window title every 0.5 seconds
        sprintf (percents, "{%s %s} - %s", percents0, remains0, strequ(r.filename,"-")? PROGRAM_NAME : r.filename);
        EnvSetConsoleTitle (percents);
        r.lasttime2 = after;
      }
      r.lasttime = after;
    }
    //r.start_time -= GetSomeTime()-after;   // Don't take into account I/O times
#endif
    return size;

  } else if (strequ(what,"done")) {
    // Print final compression statistics
    if (!r.quiet_result && r.insize && r.outsize)
    {
#ifndef FREEARC_NO_TIMING
      double time = GetSomeTime() - r.start_time;     // Time spent for (de)compression
#endif
      double insizeMB  = double(r.insize)/1000/1000;
      double outsizeMB = double(r.outsize)/1000/1000;
      double ratio     = (r.mode==COMPRESS? outsizeMB/insizeMB : insizeMB/outsizeMB) * 100;
      fprintf (stderr, "\r%s%s %.3lf -> %.3lf mb (%.1lf%%)", r.method_name, r.mode==COMPRESS? "compressed":"Unpacked", insizeMB, outsizeMB, ratio);
#ifndef FREEARC_NO_TIMING
      double speed = (r.mode==COMPRESS? insizeMB : outsizeMB) / mymax(time,0.001);
      if (time>0.001)  fprintf (stderr, ", time %.3lf secs, speed %.3lf mb/sec", time, speed);
#endif
      fprintf (stderr, "\n");
    }
#ifndef FREEARC_NO_TIMING
    if (!r.quiet_title && r.filesize)
      EnvResetConsoleTitle();
#endif
    return FREEARC_OK;

  } else {
    return FREEARC_ERRCODE_NOT_IMPLEMENTED;
  }
}


int main (int argc, char **argv)
{
    // Operation mode
    OPMODE global_mode=AUTO;

    // Record that stores all the info required for ReadWriteCallback
    static Results r;
    r.use_cpu_time = r.quiet_title = r.quiet_header = r.quiet_progress = r.quiet_result = FALSE;

    // Default compression parameters are equivalent to option -5
    r.method = std_Tornado_method [default_Tornado_method];

    // Delete successfully (de)compressed input files
    bool delete_input_files = FALSE;

    // Count of files to process
    int fcount=0;

    // Output path/filename
    const char *output_filename = NULL;

    // Process options until "--"
    // 1. First, process -0..-12 option if any
    for (char **argv_ptr = argv; *++argv_ptr!=NULL; ) {
        char *param = *argv_ptr;
        if (*param == '-') {
            param++;
                 if (strcasecmp(param,"-")==0)   break;   // "--" means "stop processing option"
            else if (isdigit(*param))            r.method = std_Tornado_method [mymin(atoi(param),elements(std_Tornado_method)-1)];
        }
    }
    // 2. Second, process rest of options
    for (char **argv_ptr = argv; *++argv_ptr!=NULL; ) {
        char *param = *argv_ptr;
        if (param[0] != '-' || param[1]=='\0') {
            fcount++;
        } else { param++;  int error=0;
                 if (strcasecmp(param,"-")==0)      break;
            else if (strcasecmp(param,"") ==0)      continue;
            else if (strcasecmp(param,"z")==0)      global_mode=COMPRESS;
            else if (strcasecmp(param,"d")==0)      global_mode=DECOMPRESS;
            else if (strcasecmp(param,"delete")==0) delete_input_files=TRUE;
            else if (strcasecmp(param,"t")==0)      output_filename="";
            else if (strcasecmp(param,"q")==0)      r.quiet_title = r.quiet_header = r.quiet_progress = r.quiet_result = TRUE;
#ifndef FREEARC_NO_TIMING
            else if (strcasecmp(param,"cpu")==0)    r.use_cpu_time=TRUE;
#endif
            else if (strcasecmp(param,"h")==0)      global_mode=HELP;
            else if (strcasecmp(param,"b")==0)      r.method.buffer=2047*mb;
            else if (strcasecmp(param,"x")==0)      r.method.caching_finder = 1;
            else if (strcasecmp(param,"xx")==0)     r.method.caching_finder = 2;
            else if (strcasecmp(param,"x+")==0)     r.method.caching_finder = 1;
            else if (strcasecmp(param,"x-")==0)     r.method.caching_finder = 0;
            else if (strcasecmp(param,"t+")==0)     r.method.find_tables = TRUE;
            else if (strcasecmp(param,"t-")==0)     r.method.find_tables = FALSE;
            else if (strcasecmp(param,"t1")==0)     r.method.find_tables = TRUE;
            else if (strcasecmp(param,"t0")==0)     r.method.find_tables = FALSE;
            else if (strcasecmp(param,"s")==0)      r.method.hash3 = 1;
            else if (strcasecmp(param,"ss")==0)     r.method.hash3 = 2;
            else if (strcasecmp(param,"s+")==0)     r.method.hash3 = 1;
            else if (strcasecmp(param,"s-")==0)     r.method.hash3 = 0;
            else if (isdigit(*param))            ; // -0..-12 option is already processed :)
            else switch( tolower(*param++) ) {
                case 'c': r.method.encoding_method = parseInt (param, &error); break;
                case 'x': r.method.caching_finder  = parseInt (param, &error); break;
                case 's': r.method.hash3           = parseInt (param, &error); break;
                case 'l': r.method.hash_row_width  = parseInt (param, &error); break;
                case 'b': r.method.buffer          = parseMem (param, &error); break;
                case 'p': r.method.match_parser    = parseInt (param, &error); break;
                case 'o': output_filename          = param;                    break;
                case 'h': r.method.hashsize        = parseMem (param, &error); break;
                case 'u': r.method.update_step     = parseInt (param, &error); break;
                case 'q':
#ifndef FREEARC_NO_TIMING
                          r.quiet_title            = strchr (param, 't');
                          r.quiet_progress         = strchr (param, 'p');
#endif
                          r.quiet_header           = strchr (param, 'h');
                          r.quiet_result           = strchr (param, 'r');
                          break;
                case 'a': switch( tolower(*param) ) {
                            case 'h': r.method.auxhash_size      = parseMem (param+1, &error);  goto check_for_errors;
                            case 'l': r.method.auxhash_row_width = parseInt (param+1, &error);  goto check_for_errors;
                          }
                          // 'a' should be last option
                default : fprintf (stderr, "\n Unknown option '%s'\n", param-2);
                          exit(1);
            }
check_for_errors:
            if (error) {
                fprintf (stderr, "\n Bad format of option value: '%s'\n", param-2);
                exit(1);
            }
        }
    }

    // No files to compress: read from stdin and write to stdout
    if (global_mode!=HELP && fcount==0 &&
       (global_mode!=AUTO  ||  !isatty(0) && !isatty(1)) ) {

        static char *_argv[] = {argv[0], (char*)"-", NULL};
        argv = _argv;
        fcount = 1;

    } else if (global_mode==HELP || fcount==0) {
        char h[100], ah[100], b[100];
        showMem (r.method.hashsize, h);
        showMem (r.method.auxhash_size, ah);
        showMem (r.method.buffer, b);
        printf( "Tornado compressor v0.5 alpha (c) Bulat.Ziganshin@gmail.com  2009-04-16\n"
                "\n"
                " Usage: tor [options and files in any order]\n"
                "   -#      -- compression level (1..%d), default %d\n", int(elements(std_Tornado_method))-1, default_Tornado_method);
        printf( "   -z      -- force compression\n"
                "   -d      -- force decompression\n"
                "   -oNAME  -- output filename/directory (default %s/%s)\n", COMPRESS_EXT, DECOMPRESS_EXT);
        printf( "   -t      -- test (de)compression (redirect output to nul)\n"
                "   -delete -- delete successfully (de)compressed input files\n"
#ifdef FREEARC_NO_TIMING
                "   -q      -- be quiet; -q[hr]* disables header/results individually\n"
#else
                "   -q      -- be quiet; -q[thpr]* disables title/header/progress/results individually\n"
                "   -cpu    -- compute raw CPU time (for benchmarking)\n"
#endif
                "   -h      -- display this help\n"
                "   --      -- stop flags processing\n"
                " \"-\" used as filename means stdin/stdout\n"
                "\n"
                " Advanced compression parameters:\n"
                "   -b#     -- buffer size, default %s\n", b);
        printf( "   -h#     -- hash size, default %s\n", h);
        printf( "   -l#     -- length of hash row (1..65536), default %d\n", r.method.hash_row_width);
        printf( "   -ah#    -- auxiliary hash size, default %s\n", ah);
        printf( "   -al#    -- auxiliary hash row length (1..65536), default %d\n", r.method.auxhash_row_width);
        printf( "   -u#     -- update step (1..999), default %d\n", r.method.update_step);
        printf( "   -c#     -- coder (1-bytes,2-bits,3-huf,4-arith), default %d\n", r.method.encoding_method);
        printf( "   -p#     -- parser (1-greedy,2-lazy), default %d\n", r.method.match_parser);
        printf( "   -x#     -- caching match finder (0-disabled,1-shifting,2-cycled,5-ht5,6-ht6,7-ht7), default %d\n", r.method.caching_finder);
        printf( "   -s#     -- 2/3-byte hash (0-disabled,1-fast,2-max), default %d\n", r.method.hash3);
        printf( "   -t#     -- table diffing (0-disabled,1-enabled), default %d\n", r.method.find_tables);
        printf( "\n"
                " Predefined methods:\n");
        for (int i=1; i<elements(std_Tornado_method); i++)
        {
            printf("   %-8d-- %s\n", -i, name(std_Tornado_method[i]));
        }
        exit(1);
    }



    // (De)compress all files given on cmdline
    bool parse_options=TRUE;  // options will be parsed until "--"
    for (char **parameters = argv; *++parameters!=NULL; )
    {
        // If options are still parsed and this argument starts with "-" - it's an option
        if (parse_options && parameters[0][0]=='-' && parameters[0][1]) {
            if (strequ(*parameters,"--"))  parse_options=FALSE;
            continue;
        }

        // Save input filename
        r.filename = *parameters;

        // Select operation mode if it was not specified on cmdline
        r.mode = global_mode != AUTO?                  global_mode :
                 end_with (r.filename, COMPRESS_EXT)?  DECOMPRESS  :
                                                       COMPRESS;
        // Extension that should be added to output filenames
        const char *MODE_EXT  =  r.mode==COMPRESS? COMPRESS_EXT : DECOMPRESS_EXT;

        // Construct output filename
        if (r.mode==BENCHMARK  ||  output_filename && strequ (output_filename, "")) {  // Redirect output to nul
            strcpy (r.outname, "");
        } else if (output_filename) {
            if (strequ(output_filename,"-"))
                strcpy (r.outname, output_filename);
            else if (is_path_char (last_char (output_filename)))
                {sprintf(r.outname, "%s%s", output_filename, drop_dirname(r.filename));  goto add_remove_ext;}
            else if (dir_exists (output_filename))
                {sprintf(r.outname, "%s%c%s", output_filename, PATH_DELIMITER, drop_dirname(r.filename));  goto add_remove_ext;}
            else
                strcpy (r.outname, output_filename);
        } else if (strequ(r.filename,"-")) {
            strcpy (r.outname, r.filename);
        } else {
            // No output filename was given on cmdline:
            //    on compression   - add COMPRESS_EXT
            //    on decompression - remove COMPRESS_EXT (and add DECOMPRESS_EXT if file already exists)
            strcpy (r.outname, r.filename);
add_remove_ext: // Remove COMPRESS_EXT on the end of name or add DECOMPRESS_EXT (unless we are in COMPRESS mode)
            if (r.mode!=COMPRESS && end_with (r.outname, COMPRESS_EXT)) {
                r.outname [strlen(r.outname) - strlen(COMPRESS_EXT)] = '\0';
                if (file_exists (r.outname))
                    strcat(r.outname, MODE_EXT);
            } else {
                strcat(r.outname, MODE_EXT);
            }
        }

        // Open input file
        r.fin = strequ (r.filename, "-")? stdin : fopen (r.filename, "rb");
        if (r.fin == NULL) {
            fprintf (stderr, "\n Can't open %s for read\n", r.filename);
            exit(2);
        }
        set_binary_mode (r.fin);

        // Open output file
        if (*r.outname) {
          r.fout = strequ (r.outname, "-")? stdout : fopen (r.outname, "wb");
          if (r.fout == NULL) {
              fprintf (stderr, "\n Can't open %s for write\n", r.outname);
              exit(2);
          }
          set_binary_mode (r.fout);
        } else {
          r.fout = NULL;
        }

        // Prepare to (de)compression
        int result;
        ReadWriteCallback ("init", NULL, 0, &r);

        // Perform actual (de)compression
        switch (r.mode) {
        case COMPRESS: {
            if (!r.quiet_header && r.filesize >= 0)
                fprintf (stderr, "Compressing %.3lf mb with %s\n", double(r.filesize)/1000/1000, name(r.method));
            PackMethod m = r.method;
            if (r.filesize >= 0)
                m.buffer = mymin (m.buffer, r.filesize+LOOKAHEAD*2);
            result = tor_compress (m, ReadWriteCallback, &r);
            break; }

        case DECOMPRESS: {
            //if (!r.quiet_header && !strequ (r.outname, "-"))   fprintf (stderr, "Unpacking %.3lf mb\n", double(r.filesize)/1000/1000);
            result = tor_decompress (ReadWriteCallback, &r);
            break; }
        }

        // Finish (de)compression
        ReadWriteCallback ("done", NULL, 0, &r);
        fclose (r.fin);
        if (r.fout)  fclose (r.fout);

        if (result == FREEARC_OK)  {
            if (delete_input_files && !strequ(r.filename,"-"))    delete_file(r.filename);
        } else {
            if (!strequ(r.outname,"-") && !strequ(r.outname,""))  delete_file(r.outname);
            switch (result) {
            case FREEARC_ERRCODE_INVALID_COMPRESSOR:
                fprintf (stderr, "\nThis compression mode isn't supported by small Tornado version, use full version instead!");
                break;
            case FREEARC_ERRCODE_NOT_ENOUGH_MEMORY:
                fprintf (stderr, "\nNot enough memory for (de)compression!");
                break;
            case FREEARC_ERRCODE_READ:
                fprintf (stderr, "\nRead error! Bad media?");
                break;
            case FREEARC_ERRCODE_WRITE:
                fprintf (stderr, "\nWrite error! Disk full?");
                break;
            case FREEARC_ERRCODE_BAD_COMPRESSED_DATA:
                fprintf (stderr, "\nData can't be decompressed!");
                break;
            default:
                fprintf (stderr, "\n(De)compression failed with error code %d!", result);
                break;
            }
            exit(3);
        }

        // going to next file...
    }

    return 0;
}

