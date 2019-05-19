/*
View xsellog log in rofi for GNU/Linux.
Seperator for data is the null byte ('\0').
Thanks to everyone for their help and code examples.
Andrey Shashanov (2018)
Dependences (Debian): libxcb1, libxcb1-dev
gcc -O2 -s -lxcb -o xsellogview xsellogview.c
Command for rofi: rofi -modi CLIPBOARD:xsellogview -show CLIPBOARD
*/

/* ftruncate(), sigemptyset(), sigaction() */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#else
#if (_POSIX_C_SOURCE < 200809L)
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb.h>

#include "common.h"

#define MAX_LABEL_SIZE 256

int main(int argc, char *argv[])
{
    char *pathname = getpathname();

    struct stat statbuf;
    if (stat(pathname, &statbuf) == -1)
    {
        free(pathname);
        return EXIT_FAILURE;
    }

    size_t file_size;
    if ((file_size = (size_t)statbuf.st_size) == 0)
    {
        free(pathname);
        return EXIT_SUCCESS;
    }

    if (argc == 1)
    {
        /* OUTPUT CLIPS LABELS LIST */

        char label[MAX_LABEL_SIZE];
        size_t i_ch = 0;
        char *plist = 0;
        size_t sizelist = 0;
        ssize_t n_list = -1, i_label = 0;

        FILE *fp = fopen(pathname, "r+b");
        if (fp == NULL)
        {
            fprintf(stderr, "%s: %s %s\n", argv[0], strerror(errno), pathname);
            free(pathname);
            return EXIT_FAILURE;
        }
        free(pathname);

        for (;;)
        {
            char *plistre;

            int ch = fgetc(fp);

            if (ch == EOF)
                break;
            else if (ch == '\n')
                ch = ' ';
            else if (ch == '\0')
            {
                if (i_ch >= MAX_LABEL_SIZE - 7)
                {
                    label[i_ch - 1] = '\n';
                    label[i_ch] = '\0';
                }
                else
                {
                    label[i_ch] = '\n';
                    label[i_ch + 1] = '\0';
                }

                sizelist += MAX_LABEL_SIZE;
                plistre = (char *)realloc(plist, sizelist);
                if (plistre == NULL)
                {
                    fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
                    fclose(fp);
                    if (plist != NULL)
                        free(plist);
                    return EXIT_FAILURE;
                }
                else
                    plist = plistre;

                memmove(&plist[sizelist - MAX_LABEL_SIZE],
                        label, MAX_LABEL_SIZE);

                ++i_label;
                i_ch = 0;
                ch = fgetc(fp);
                if (ch == '\n')
                    ch = ' ';
            }
            else if (i_ch >= MAX_LABEL_SIZE - 7)
            {
                label[i_ch] = '\n';
                label[i_ch + 1] = '\0';

                sizelist += MAX_LABEL_SIZE;
                plistre = (char *)realloc(plist, sizelist);
                if (plistre == NULL)
                {
                    fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
                    fclose(fp);
                    if (plist != NULL)
                        free(plist);
                    return EXIT_FAILURE;
                }
                else
                    plist = plistre;

                memmove(&plist[sizelist - MAX_LABEL_SIZE],
                        label, MAX_LABEL_SIZE);

                ++i_label;
                i_ch = 0;

                do
                {
                    ch = fgetc(fp);
                    if (ch == '\0')
                    {
                        ch = fgetc(fp);
                        if (ch == '\n')
                            ch = ' ';
                        break;
                    }
                } while (ch != EOF);
            }

            label[i_ch] = (char)ch;
            ++i_ch;
        }

        fclose(fp);

        do
        {
            fprintf(stdout, "%3zd. %s",
                    ++n_list, &plist[--i_label * MAX_LABEL_SIZE]);
        } while (i_label);

        free(plist);
    }
    else
    {
        /* OUTPUT SELECTED CLIP */

        ssize_t i_label = atoi(argv[1]);
        if (i_label < 0)
            i_label = 0;

        int fd = open(pathname, O_RDWR | O_SYNC);
        if (fd == -1)
        {
            fprintf(stderr, "%s: %s %s\n", argv[0], strerror(errno), pathname);
            free(pathname);
            return EXIT_FAILURE;
        }
        free(pathname);

        char *fpmem = (char *)mmap(NULL, file_size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);

        size_t offset1 = 0, offset2 = 0, size_buf;
        ssize_t n_list = -1;
        size_t i = file_size;
        do
        {
            --i;
            if (fpmem[i] == '\0')
            {
                if (n_list == i_label)
                    break;
                else
                    ++n_list;
                offset2 = i;
            }
            else
                offset1 = i;
        } while (i);

        /* + 1 only for null byte */
        ++offset2;

        size_buf = offset2 - offset1;

        char *buf = (char *)malloc(size_buf);
        if (buf == NULL)
        {
            fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }

        memmove(buf, fpmem + offset1, size_buf);

        /* deletes selected clip, except the last clip (0) */
        if (i_label != 0)
        {
            memmove(fpmem + offset1, fpmem + offset2, file_size - offset2);
            munmap(fpmem, file_size);
            ftruncate(fd, (off_t)(offset1 + (file_size - offset2)));
        }
        else
            munmap(fpmem, file_size);

        close(fd);

        if (isatty(STDOUT_FILENO))
            /* output to stdout only if run from a terminal */
            fprintf(stdout, "%s\n", buf);
        else
            /* required only for the rofi (close pipe) */
            freopen("/dev/stderr", "ab", stdout);

        /* remove null byte */
        --size_buf;

        xcb_connection_t *c = xcb_connect(NULL, NULL);
        if (xcb_connection_has_error(c) != 0)
        {
            free(buf);
            return EXIT_FAILURE;
        }

        xcb_intern_atom_cookie_t atom_c_1 =
            xcb_intern_atom_unchecked(c, 0, strlen(SELECTION), SELECTION);
        xcb_intern_atom_cookie_t atom_c_2 =
            xcb_intern_atom_unchecked(c, 0, 4, "INCR");
        xcb_intern_atom_cookie_t atom_c_3 =
            xcb_intern_atom_unchecked(c, 0, 11, "UTF8_STRING");
        xcb_intern_atom_cookie_t atom_c_4 =
            xcb_intern_atom_unchecked(c, 0, 7, "TARGETS");

        xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

        uint32_t value_m = XCB_CW_BACK_PIXEL |
                           XCB_CW_OVERRIDE_REDIRECT |
                           XCB_CW_EVENT_MASK;

        const uint32_t value_l[] = {s->black_pixel,
                                    1,
                                    XCB_EVENT_MASK_PROPERTY_CHANGE};

        xcb_window_t w = xcb_generate_id(c);

        xcb_create_window(c,
                          XCB_COPY_FROM_PARENT,
                          w,
                          s->root,
                          0, 0,
                          1, 1,
                          0,
                          XCB_COPY_FROM_PARENT,
                          s->root_visual,
                          value_m,
                          value_l);

        xcb_change_property(c,
                            XCB_PROP_MODE_REPLACE,
                            w,
                            XCB_ATOM_WM_NAME,
                            XCB_ATOM_STRING,
                            8,
                            (uint32_t)strlen(argv[0]),
                            argv[0]);

        xcb_intern_atom_reply_t *atom_r_1 =
            xcb_intern_atom_reply(c, atom_c_1, NULL);
        xcb_atom_t XCB_ATOM_SELECTION = atom_r_1->atom;
        free(atom_r_1);
        xcb_intern_atom_reply_t *atom_r_2 =
            xcb_intern_atom_reply(c, atom_c_2, NULL);
        xcb_atom_t XCB_ATOM_INCR = atom_r_2->atom;
        free(atom_r_2);
        xcb_intern_atom_reply_t *atom_r_3 =
            xcb_intern_atom_reply(c, atom_c_3, NULL);
        xcb_atom_t XCB_ATOM_UTF8_STRING = atom_r_3->atom;
        free(atom_r_3);
        xcb_intern_atom_reply_t *atom_r_4 =
            xcb_intern_atom_reply(c, atom_c_4, NULL);
        xcb_atom_t XCB_ATOM_TARGETS = atom_r_4->atom;
        free(atom_r_4);

        xcb_atom_t TARGETS[] = {XCB_ATOM_TARGETS,
                                XCB_ATOM_UTF8_STRING,
                                XCB_ATOM_STRING};

        xcb_set_selection_owner(c, w, XCB_ATOM_SELECTION, XCB_CURRENT_TIME);

        xcb_get_selection_owner_cookie_t selection_owner_c =
            xcb_get_selection_owner_unchecked(c, XCB_ATOM_SELECTION);

        xcb_get_selection_owner_reply_t *selection_owner_r =
            xcb_get_selection_owner_reply(c, selection_owner_c, NULL);

        if (selection_owner_r == NULL || selection_owner_r->owner != w)
        {
            if (selection_owner_r != NULL)
                free(selection_owner_r);
            free(buf);
            xcb_destroy_window(c, w);
            xcb_disconnect(c);
            fprintf(stderr, "%s: Cannot set X11 selection owner\n", argv[0]);
            return EXIT_FAILURE;
        }
        free(selection_owner_r);

        xcb_flush(c);

        xcb_atom_t target = XCB_ATOM_NONE;
        uint32_t incr = 0;
        uint32_t pos = 0;
        uint32_t clear = 0;
        xcb_generic_event_t *e;

        pid_t pid = fork();
        if (pid > 0)
        {
            free(buf);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1)
        {
            free(buf);
            xcb_destroy_window(c, w);
            xcb_disconnect(c);
            return EXIT_FAILURE;
        }

        setsid();

        struct sigaction act;
        sigemptyset(&(act.sa_mask));
        act.sa_handler = SIG_IGN;
        act.sa_flags = 0;
        sigaction(SIGCHLD, &act, NULL);
        sigaction(SIGHUP, &act, NULL);

        pid = fork();
        if (pid > 0)
        {
            free(buf);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1)
        {
            free(buf);
            xcb_destroy_window(c, w);
            xcb_disconnect(c);
            return EXIT_FAILURE;
        }

        umask(0);

        freopen("/dev/null", "rb", stdin);
        freopen("/dev/null", "ab", stdout);
        freopen("/dev/null", "ab", stderr);

        chdir("/");

        while ((e = xcb_wait_for_event(c)) != NULL)
        {
            uint32_t request_length = 65535 * 4;
            uint32_t maximum_request_length;
            maximum_request_length = xcb_get_maximum_request_length(c) * 4;
            if ((maximum_request_length -= 100) > request_length)
                maximum_request_length = request_length;

            switch (incr)
            {
            case 0:
                if (XCB_EVENT_RESPONSE_TYPE(e) != XCB_SELECTION_REQUEST)
                    break;

                xcb_selection_request_event_t *selection_request_e;
                selection_request_e = (xcb_selection_request_event_t *)e;

                pos = 0;

                xcb_selection_notify_event_t selection_notify_e;
                selection_notify_e.pad0 = 0;
                selection_notify_e.property = selection_request_e->property;
                selection_notify_e.requestor = selection_request_e->requestor;
                selection_notify_e.response_type = XCB_SELECTION_NOTIFY;
                selection_notify_e.selection = selection_request_e->selection;
                selection_notify_e.sequence = 0;
                selection_notify_e.target = selection_request_e->target;
                selection_notify_e.time = selection_request_e->time;

                if (selection_request_e->target == XCB_ATOM_TARGETS)
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_REPLACE,
                                        selection_request_e->requestor,
                                        selection_request_e->property,
                                        XCB_ATOM_ATOM,
                                        32,
                                        sizeof(TARGETS) / sizeof(xcb_atom_t),
                                        TARGETS);
                }
                else if (selection_request_e->target != XCB_ATOM_UTF8_STRING &&
                         selection_request_e->target != XCB_ATOM_STRING)
                {
                    selection_notify_e.property = XCB_ATOM_NONE;
                }
                else if (size_buf > maximum_request_length)
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_REPLACE,
                                        selection_request_e->requestor,
                                        selection_request_e->property,
                                        XCB_ATOM_INCR,
                                        32,
                                        0,
                                        NULL);

                    const uint32_t value[] = {XCB_EVENT_MASK_PROPERTY_CHANGE};
                    xcb_change_window_attributes(c,
                                                 selection_request_e->requestor,
                                                 XCB_CW_EVENT_MASK,
                                                 value);

                    target = selection_request_e->target;
                    incr = 1;
                }
                else
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_REPLACE,
                                        selection_request_e->requestor,
                                        selection_request_e->property,
                                        selection_request_e->target,
                                        8,
                                        (uint32_t)size_buf,
                                        buf);
                }

                xcb_send_event(c,
                               0,
                               selection_request_e->requestor,
                               XCB_EVENT_MASK_NO_EVENT,
                               (const char *)&selection_notify_e);
                break;

            case 1:
                if (XCB_EVENT_RESPONSE_TYPE(e) != XCB_PROPERTY_NOTIFY)
                    break;

                xcb_property_notify_event_t *property_notify_e;
                property_notify_e = (xcb_property_notify_event_t *)e;

                if (property_notify_e->state != XCB_PROPERTY_DELETE)
                    break;

                uint32_t increment_len = maximum_request_length;

                if ((pos + increment_len) > size_buf)
                    increment_len = (uint32_t)size_buf - pos;

                if (pos > size_buf)
                    increment_len = 0;

                if (pos == 0)
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_REPLACE,
                                        property_notify_e->window,
                                        property_notify_e->atom,
                                        target,
                                        8,
                                        increment_len,
                                        buf + pos);
                }
                else if (increment_len != 0)
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_APPEND,
                                        property_notify_e->window,
                                        property_notify_e->atom,
                                        target,
                                        8,
                                        increment_len,
                                        buf + pos);
                }
                else
                {
                    xcb_change_property(c,
                                        XCB_PROP_MODE_APPEND,
                                        property_notify_e->window,
                                        property_notify_e->atom,
                                        target,
                                        8,
                                        0,
                                        NULL);

                    const uint32_t value[] = {XCB_EVENT_MASK_NO_EVENT};
                    xcb_change_window_attributes(c,
                                                 property_notify_e->window,
                                                 XCB_CW_EVENT_MASK,
                                                 value);
                }

                if (increment_len == 0)
                    incr = 0;
                else
                    pos += maximum_request_length;
            }

            xcb_flush(c);

            if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_CLEAR)
                clear = 1;

            free(e);

            if (!incr && clear)
                break;
        }

        free(buf);
        xcb_destroy_window(c, w);
        xcb_disconnect(c);
    }

    return EXIT_SUCCESS;
}
