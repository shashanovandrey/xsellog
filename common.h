/* "CLIPBOARD" or "PRIMARY" */
#define SELECTION "CLIPBOARD"

#define PART_NAME_OF_LOGFILE "/xsellog-"

static char *getpathname(void);

char *getpathname(void)
{
    struct passwd *pw;
    char *dir, *pathname;

    pw = getpwuid(geteuid());

    dir = getenv("TMPDIR");
    if (dir == NULL || dir[0] == '\0')
        dir = (char *)"/tmp";

    pathname = (char *)malloc(sizeof dir - 1 +
                              sizeof PART_NAME_OF_LOGFILE - 1 +
                              sizeof pw->pw_name);

    strcpy(pathname, dir);
    strcat(pathname, PART_NAME_OF_LOGFILE);
    strcat(pathname, pw->pw_name);

    return pathname;
}
