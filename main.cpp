#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "main.h"

static void verror_msg(const char *s, va_list p)
{
    fflush(stdout);
    vfprintf(stderr, s, p);
}

static void error_msg(const char *s, ...)
{
    va_list p;
    va_start(p, s);
    verror_msg(s, p);
    va_end(p);
    putc('\n', stderr);
}

static void error_msg_and_die(const char *s, ...)
{
    va_list p;
    va_start(p, s);
    verror_msg(s, p);
    va_end(p);
    putc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void vperror_msg(const char *s, va_list p)
{
    int err = errno;
    if (s == 0)
        s = "";
    verror_msg(s, p);
    if (*s)
        s = ": ";
    fprintf(stderr, "%s%s\n", s, strerror(err));
}

static void perror_msg_and_die(const char *s, ...)
{
    va_list p;
    va_start(p, s);
    vperror_msg(s, p);
    va_end(p);
    exit(EXIT_FAILURE);
}

static FILE * xfopen(const char *path, const char *mode)
{
    FILE *fp;

    if ((fp = fopen(path, mode)) == NULL)
        perror_msg_and_die("%s", path);

    return fp;
}

static void populate_fs(char **dopt, int didx, int verbose)
{
    int i;
    char * pdest;
    struct stat st;
    int pdir;

    for (i = 0; i < didx; i++)
    {
        // If a [:path] is given, change to that directory in the image.
        if((pdest = strchr(dopt[i], ':')))
        {
            *(pdest++) = 0;
            if (f_chdir(pdest) != FR_OK)
                error_msg_and_die("path %s not found in file system", pdest);
        }

        stat(dopt[i], &st);

        switch(st.st_mode & S_IFMT)
        {
            case S_IFREG:
                error_msg_and_die("Adding from file %s is not supported", dopt[i]);
                break;

            case S_IFDIR:
                if ((pdir = open(".", O_RDONLY)) < 0)
                    perror_msg_and_die(".");

                if (chdir(dopt[i]) < 0)
                    perror_msg_and_die(dopt[i]);

//                add2fs_from_dir(verbose, 1);

                if (fchdir(pdir) < 0)
                    perror_msg_and_die("fchdir");

                if (close(pdir) < 0)
                    perror_msg_and_die("close");
                break;

            default:
                error_msg_and_die("%s is neither a file nor a directory", dopt[i]);
        }
    }
}

#define MAX_DOPT 128

//int main() {
//    int nbblocks = -1;
//    char *fsout = "testdisk";
//    char *dopt[MAX_DOPT];
//    BYTE buff[262144];
//    int didx = 0;
//    int c;
//    int verbose = 1;
//    FATFS Fatfs;    /* File system object */
//    FILE *fp = NULL;
//    DiskMap* disk = new DiskMap;
//
//    uint32_t fileSize = 209715200;
//
//    disk_image_array_init(disk, fileSize);
//
//    f_mount(&Fatfs, "", 0);
//
//    if (verbose) {
//        printf("Creating fat file system\n");
//    }
//
//    MKFS_PARM opt = { FM_FAT32 };
//    int res = f_mkfs("", &opt, buff, sizeof buff);
//    if (res != FR_OK)
//        perror_msg_and_die("Error making fat file system");
//
//    if (verbose) {
//        printf("Populating fat file system. Please wait...\n");
//    }
//
//    populate_fs(dopt, didx, verbose);
//
//    if (verbose) {
//        printf("Done\n");
//    }
//
//    FIL f_out;
//    if (f_open(&f_out, "testtest.txt", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
//        perror_msg_and_die("Error creating in image");
//
//    UINT bw;
//    f_expand(&f_out, 1024*128-32, 1);
//
//    f_write(&f_out, "\0", 1, &bw);
//
//    f_close(&f_out);
//
//    if (f_open(&f_out, "test2", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
//        perror_msg_and_die("Error creating in image");
//
//    f_expand(&f_out, 1024*32, 1);
//
//    f_write(&f_out, "\0", 1, &bw);
//
//    f_close(&f_out);
//
//    f_mount(nullptr, "", 0);
//
//    return 0;
//}
