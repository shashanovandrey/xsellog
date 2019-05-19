/*
X server selection CLIPBOARD or PRIMARY log for GNU/Linux.
Seperator for data is the null byte ('\0').
Thanks to everyone for their help and code examples.
Andrey Shashanov (2018)
Dependences (Debian): libxcb1, libxcb1-dev, zlib1g, zlib1g-dev
gcc -O2 -s -lxcb -lxcb-xfixes -lz -o xsellog xsellog.c
*/

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <zlib.h>

#include "common.h"

#define RETURN_SUCCESS 0
#define RETURN_CONVERT_REFUSED 1
#define RETURN_WRITE_LOG_ERROR 2
#define RETURN_WRITE_LOG_NEXT 4
#define RETURN_WRITE_LOG_NO_NEED 8
#define RETURN_NULL_EVENT 16
#define RETURN_ERROR_GET_PROPERTY 32

static char *progname;
static int fd;

static int write_to_log(size_t n, char *d, int incr)
{
    static uLong crc_prev = 0;
    static size_t n_incr = 0;
    static char *pmem = NULL;
    uLong crc;
    char *premem;
    char d_end = '\0';

    if (!incr)
        crc = crc32(crc32(0L, Z_NULL, 0), (Bytef *)d, (uInt)n);
    else if (n != 0)
    {
        n_incr += n;
        premem = (char *)realloc(pmem, n_incr);
        if (premem != NULL)
            pmem = premem;
        else if (pmem != NULL)
        {
            fprintf(stderr, "%s: %s\n", progname, strerror(errno));
            free(pmem);
            pmem = NULL;
            n_incr = 0;
            crc = 0;
            return RETURN_WRITE_LOG_ERROR;
        }
        memmove(&pmem[n_incr - n], d, n);
        return RETURN_WRITE_LOG_NEXT;
    }
    else
        crc = crc32(crc32(0L, Z_NULL, 0), (Bytef *)pmem, (uInt)n_incr);

    if (crc_prev == crc)
    {
        crc = 0;
        if (n_incr != 0)
        {
            free(pmem);
            pmem = NULL;
            n_incr = 0;
        }
        return RETURN_WRITE_LOG_NO_NEED;
    }

    if (n_incr == 0)
    {
        write(fd, d, n);
        write(fd, &d_end, sizeof d_end);
    }
    else
    {
        write(fd, pmem, n_incr);
        write(fd, &d_end, sizeof d_end);
        free(pmem);
        pmem = NULL;
        n_incr = 0;
    }

    crc_prev = crc;
    crc = 0;
    return RETURN_SUCCESS;
}

static int print_selection(xcb_connection_t *c, xcb_window_t w,
                           xcb_atom_t atom_selection,
                           xcb_atom_t atom_property,
                           xcb_atom_t atom_incr,
                           xcb_atom_t atom_target)
{
    xcb_generic_event_t *e;
    xcb_selection_notify_event_t *selection_notify_e;
    xcb_property_notify_event_t *property_notify_e;
    xcb_get_property_cookie_t property_c;
    xcb_get_property_reply_t *property_r;
    char *value;
    size_t value_len;

    xcb_convert_selection(c, w,
                          atom_selection,
                          atom_target,
                          atom_property,
                          XCB_CURRENT_TIME);
    xcb_flush(c);

    for (;;)
    {
        if ((e = xcb_wait_for_event(c)) != NULL)
            if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_NOTIFY)
                break;
            else
                free(e);
        else
            return RETURN_NULL_EVENT;
    }

    selection_notify_e = (xcb_selection_notify_event_t *)e;

    if (selection_notify_e->property == XCB_ATOM_NONE)
    {
        free(e);
        return RETURN_CONVERT_REFUSED;
    }

    property_c = xcb_get_property_unchecked(c,
                                            1,
                                            selection_notify_e->requestor,
                                            selection_notify_e->property,
                                            XCB_GET_PROPERTY_TYPE_ANY,
                                            0,
                                            UINT32_MAX);

    property_r = xcb_get_property_reply(c, property_c, NULL);
    if (property_r == NULL)
    {
        free(e);
        return RETURN_ERROR_GET_PROPERTY;
    }

    if (property_r->type != atom_incr)
    {
        value = (char *)xcb_get_property_value(property_r);
        value_len = (size_t)xcb_get_property_value_length(property_r);

        if (value_len)
            write_to_log(value_len, value, 0);

        free(property_r);
        free(e);
        return RETURN_SUCCESS;
    }

    do
    {
        free(property_r);
        free(e);

        for (;;)
        {
            if ((e = xcb_wait_for_event(c)) != NULL)
            {

                if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_PROPERTY_NOTIFY)
                {
                    property_notify_e = (xcb_property_notify_event_t *)e;
                    if (property_notify_e->state == XCB_PROPERTY_NEW_VALUE)
                        break;
                }
                else
                    free(e);
            }
            else
                return RETURN_NULL_EVENT;
        }

        property_c = xcb_get_property_unchecked(c,
                                                1,
                                                property_notify_e->window,
                                                property_notify_e->atom,
                                                XCB_GET_PROPERTY_TYPE_ANY,
                                                0,
                                                UINT32_MAX);

        property_r = xcb_get_property_reply(c, property_c, NULL);
        if (property_r == NULL)
        {
            xcb_delete_property(c, property_notify_e->window,
                                property_notify_e->atom);
            free(e);
            return RETURN_ERROR_GET_PROPERTY;
        }

        value = (char *)xcb_get_property_value(property_r);
        value_len = (size_t)xcb_get_property_value_length(property_r);

        if (write_to_log(value_len, value, 1) == RETURN_WRITE_LOG_ERROR)
        {
            xcb_delete_property(c, property_notify_e->window,
                                property_notify_e->atom);
            free(property_r);
            free(e);
            return RETURN_WRITE_LOG_ERROR;
        }

    } while (value_len);
    xcb_delete_property(c, property_notify_e->window,
                        property_notify_e->atom);
    free(property_r);
    free(e);
    return RETURN_SUCCESS;
}

int main(int argc, char *argv[])
{
    char *pathname;
    struct flock fl;
    struct passwd *pw;
    xcb_connection_t *c;
    const xcb_query_extension_reply_t *xfixes_data;
    xcb_xfixes_query_version_cookie_t version_c;
    xcb_window_t w;
    xcb_screen_t *s;
    uint32_t value_m;
    uint32_t value_l[3];
    xcb_intern_atom_cookie_t atom_c_1, atom_c_2, atom_c_3, atom_c_4;
    xcb_intern_atom_reply_t *atom_r_1, *atom_r_2, *atom_r_3, *atom_r_4;
    xcb_atom_t XCB_ATOM_SELECTION, XCB_ATOM_XSEL_DATA, XCB_ATOM_INCR,
        XCB_ATOM_UTF8_STRING;
    uint32_t event_m;

    progname = argv[0];
    pathname = getpathname();

    if (argc == 2 && strcmp(argv[1], "-h") == 0)
    {
        fprintf(stdout, "Log X server selection: %s\nLog file: %s\n",
                SELECTION, pathname);
        free(pathname);
        return EXIT_SUCCESS;
    }

    if ((fd = open(pathname,
                   O_WRONLY | O_CREAT | O_APPEND | O_SYNC,
                   S_IRUSR | S_IWUSR)) == -1)
    {
        fprintf(stderr, "%s: %s %s\n", progname, strerror(errno), pathname);
        free(pathname);
        return EXIT_FAILURE;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = 0;
    if (fcntl(fd, F_SETLK, &fl) == -1)
    {
        fcntl(fd, F_GETLK, &fl);
        close(fd);
        fprintf(stdout, "%s: Another instance is running with PID %i\n",
                progname, fl.l_pid);
        return EXIT_FAILURE;
    }

    pw = getpwuid(geteuid());
    if (chown(pathname, pw->pw_uid, pw->pw_gid) == -1 ||
        chmod(pathname, S_IRUSR | S_IWUSR) == -1)
    {
        fprintf(stderr, "%s: %s %s\n", progname, strerror(errno), pathname);
        close(fd);
        free(pathname);
        return EXIT_FAILURE;
    }

    free(pathname);

    c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c))
    {
        close(fd);
        return EXIT_FAILURE;
    }

    xcb_prefetch_extension_data(c, &xcb_xfixes_id);

    atom_c_1 = xcb_intern_atom_unchecked(c, 0, strlen(SELECTION), SELECTION);
    atom_c_2 = xcb_intern_atom_unchecked(c, 0, 9, "XSEL_DATA");
    atom_c_3 = xcb_intern_atom_unchecked(c, 0, 4, "INCR");
    atom_c_4 = xcb_intern_atom_unchecked(c, 0, 11, "UTF8_STRING");

    s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

    value_m = XCB_CW_BACK_PIXEL |
              XCB_CW_OVERRIDE_REDIRECT |
              XCB_CW_EVENT_MASK;

    value_l[0] = s->black_pixel;
    value_l[1] = 1;
    value_l[2] = XCB_EVENT_MASK_PROPERTY_CHANGE;

    w = xcb_generate_id(c);

    xcb_create_window(c,
                      XCB_COPY_FROM_PARENT,
                      w,
                      s->root,
                      0, 0,
                      1, 1,
                      0,
                      XCB_WINDOW_CLASS_COPY_FROM_PARENT,
                      s->root_visual,
                      value_m,
                      value_l);

    xcb_change_property(c,
                        XCB_PROP_MODE_REPLACE,
                        w,
                        XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING,
                        8,
                        (uint32_t)strlen(progname),
                        progname);

    atom_r_1 = xcb_intern_atom_reply(c, atom_c_1, NULL);
    XCB_ATOM_SELECTION = atom_r_1->atom;
    free(atom_r_1);
    atom_r_2 = xcb_intern_atom_reply(c, atom_c_2, NULL);
    XCB_ATOM_XSEL_DATA = atom_r_2->atom;
    free(atom_r_2);
    atom_r_3 = xcb_intern_atom_reply(c, atom_c_3, NULL);
    XCB_ATOM_INCR = atom_r_3->atom;
    free(atom_r_3);
    atom_r_4 = xcb_intern_atom_reply(c, atom_c_4, NULL);
    XCB_ATOM_UTF8_STRING = atom_r_4->atom;
    free(atom_r_4);

    xfixes_data = xcb_get_extension_data(c, &xcb_xfixes_id);
    if (xfixes_data == NULL || !xfixes_data->present)
    {
        close(fd);
        xcb_destroy_window(c, w);
        xcb_disconnect(c);
        return EXIT_FAILURE;
    }

    version_c = xcb_xfixes_query_version_unchecked(c,
                                                   XCB_XFIXES_MAJOR_VERSION,
                                                   XCB_XFIXES_MINOR_VERSION);
    xcb_discard_reply(c, version_c.sequence);

    event_m = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
              XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
              XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;

    xcb_xfixes_select_selection_input(c, w, XCB_ATOM_SELECTION, event_m);

    xcb_flush(c);

    for (;;)
    {
        xcb_generic_event_t *e;
        if ((e = xcb_wait_for_event(c)) != NULL)
        {
            if (XCB_EVENT_RESPONSE_TYPE(e) ==
                (xfixes_data->first_event + XCB_XFIXES_SELECTION_NOTIFY))
            {
                if (print_selection(c, w,
                                    XCB_ATOM_SELECTION,
                                    XCB_ATOM_XSEL_DATA,
                                    XCB_ATOM_INCR,
                                    XCB_ATOM_UTF8_STRING) ==
                    RETURN_CONVERT_REFUSED)
                    print_selection(c, w,
                                    XCB_ATOM_SELECTION,
                                    XCB_ATOM_XSEL_DATA,
                                    XCB_ATOM_INCR,
                                    XCB_ATOM_STRING);
            }
            free(e);
        }
    }

    /* code will never be executed
    close(fd);
    xcb_destroy_window(c, w);
    xcb_disconnect(c);
    return EXIT_SUCCESS;
    */
}
