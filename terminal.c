/*
 * Copyright 2023 paulguy <paulguy119@gmail.com>
 *
 * This file is part of jamstikctl.
 *
 * jamstikctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * jamstikctl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with jamstikctl.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <curses.h>
#include <termios.h>
#include <term.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <wchar.h>

/* TODO: redo everything */

typedef struct {
    int only_print;

    int stdout_fd;
    int stdin_fd;
    struct termios original_termios;
    struct sigaction ohup;
    struct sigaction oint;
    struct sigaction oterm;

    int termlines, termcolumns;

    char *home;
    char *clear;
    char *dl1;
    char *il1;

    char *laststr;
    int lastlen;
    int lastlines;
} terminal_ctx_t;

terminal_ctx_t termctx;

void term_home() {
    fputs(termctx.home, stdout);
}

void term_bottom() {
    int i;

    term_home();
    for(i = 0; i < termctx.termlines; i++) {
        fputc('\n', stdout);
    }
}

void term_clear() {
    fputs(termctx.clear, stdout);
}

void term_clear_line() {
    fputs(termctx.dl1, stdout);
    fputs(termctx.il1, stdout);
}

void term_cleanup() {
    int ret;
    unsigned int termlines;
    unsigned int i;

    /* push status messages past where the terminal may clear it away */
    if(!termctx.only_print) {
        termlines = tigetnum("lines");
        for(i = 0; i < termlines * 2; i++) {
            fputc('\n', stdout);
        }
    }

    ret = tcsetattr(termctx.stdout_fd, TCSADRAIN, &termctx.original_termios);
    if(ret < 0) {
        fprintf(stderr, "Couldn't reset termios: %s\n", strerror(errno));
    }

    if(sigaction(SIGHUP, &termctx.ohup, NULL) != 0 ||
       sigaction(SIGINT, &termctx.oint, NULL) != 0 ||
       sigaction(SIGTERM, &termctx.oterm, NULL) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }
}

static void term_cleanup_handler(int signum) {
    term_cleanup();
    if(signum == SIGHUP && termctx.ohup.sa_handler != NULL) {
        termctx.ohup.sa_handler(signum);
    } else if(signum == SIGINT && termctx.oint.sa_handler != NULL) {
        termctx.oint.sa_handler(signum);
    } else if(signum == SIGTERM && termctx.oterm.sa_handler != NULL) {
        termctx.oterm.sa_handler(signum);
    }
}

int term_setup(int only_print) {
    struct sigaction sa;
    sa.sa_handler = term_cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    struct termios new_termios;

    termctx.only_print = only_print;
    termctx.stdout_fd = fileno(stdout);
    termctx.stdin_fd = fileno(stdin);
    termctx.laststr = NULL;
    termctx.lastlines = 0;

    if(tcgetattr(termctx.stdout_fd, &termctx.original_termios) < 0) {
        fprintf(stderr, "Couldn't get termios: %s\n", strerror(errno));
        return(-1);
    }

    if(sigaction(SIGHUP, &sa, &termctx.ohup) != 0 ||
       sigaction(SIGINT, &sa, &termctx.oint) != 0 ||
       sigaction(SIGTERM, &sa, &termctx.oterm) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }

    memcpy(&new_termios, &termctx.original_termios, sizeof(struct termios));
    cfmakeraw(&new_termios);
    /* allow stuff like CTRL+C to work */
    new_termios.c_lflag |= ISIG;
    /* allow printed text to not look weird */
    new_termios.c_oflag |= OPOST;
    new_termios.c_cc[VMIN] = 1;
    new_termios.c_cc[VTIME] = 0;

    if(tcsetattr(termctx.stdout_fd, TCSADRAIN, &new_termios) < 0) {
        fprintf(stderr, "Couldn't set termios: %s\n", strerror(errno));
        return(-1);
    }

    if(!termctx.only_print) {
        if(setupterm(NULL, termctx.stdout_fd, NULL) == ERR) {
            return(-1);
        }

        termctx.home = tigetstr("home");
        termctx.clear = tigetstr("clear");
        termctx.dl1 = tigetstr("dl1");
        termctx.il1 = tigetstr("il1");
        termctx.termlines = tigetnum("lines");
        termctx.termcolumns = tigetnum("columns");

        term_clear();
    }

    return(0);
}

int term_print_mode() {
    return(termctx.only_print);
}

int term_getkey() {
    fd_set readset;
    struct timeval tv;

    FD_ZERO(&readset);
    FD_SET(termctx.stdin_fd, &readset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    select(termctx.stdin_fd+1, &readset, 0, 0, &tv);
    if(FD_ISSET(termctx.stdin_fd, &readset)) {
        return(fgetc(stdin));
    }

    return(-1);
}

int term_count_lines(int n, const char *str) {
    int strlines = 0;
    int strcolumns = 0;
    int i;
    wchar_t c;
    int width;

    for(i = 0; i < n; i++) {
        /* convert unicode to wchar_t */
        if(str[i] & 0x80) {
            if((str[i] & 0xE0) == 0xC0) {
                if(i >= n - 1) {
                    return(-1);
                }
                c = ((str[i] & 0x1F) << 6) |
                    (str[i+1] & 0x3F);
                i+=1;
            } else if((str[i] & 0xF0) == 0xE0) {
                if(i >= n - 2) {
                    return(-1);
                }
                c = ((str[i] & 0x0F) << 12) |
                    ((str[i+1] & 0x3F) << 6) |
                    (str[i+2] & 0x3F);
                i+=2;
            } else if((str[i] & 0xF8) == 0xF0) {
                if(i >= n - 3) {
                    return(-1);
                }
                c = ((str[i] & 0x07) << 18) |
                    ((str[i+1] & 0x3F) << 12) |
                    ((str[i+2] & 0x3F) << 6) |
                    (str[i+3] & 0x3F);
                i+=3;
            } else if((str[i] & 0xFC) == 0xF8) {
                if(i >= n - 4) {
                    return(-1);
                }
                c = ((str[i] & 0x03) << 24) |
                    ((str[i+1] & 0x3F) << 18) |
                    ((str[i+2] & 0x3F) << 12) |
                    ((str[i+3] & 0x3F) << 6) |
                    (str[i+4] & 0x3F);
                i+=4;
            } else if((str[i] & 0xFE) == 0xFC) {
                if(i >= n - 5) {
                    return(-1);
                }
                c = ((str[i] & 0x01) << 30) |
                    ((str[i+1] & 0x3F) << 24) |
                    ((str[i+2] & 0x3F) << 18) |
                    ((str[i+3] & 0x3F) << 12) |
                    ((str[i+4] & 0x3F) << 6) |
                    (str[i+5] & 0x3F);
                i+=5;
            } else {
                return(-1);
            }
        } else {
            c = str[i];
        }

        width = wcwidth(c);

        strcolumns += width;
        if(strcolumns == termctx.termcolumns) {
            strlines++;
            strcolumns = 0;
        } else if(strcolumns > termctx.termcolumns) {
            strlines++;
            strcolumns = width;
        }
    }

    return(strlines);
}

int term_clear_status_area() {
    int termlines;
    int temp;
    int i;

    termlines = term_count_lines(termctx.lastlen, termctx.laststr);
    if(termlines < 0) {
        return(-1);
    }

    if(termctx.lastlines > termlines) {
        /* get the larger of the two but still store the new value */
        temp = termctx.lastlines;
        termctx.lastlines = termlines;
        termlines = temp;
    } else {
        termctx.lastlines = termlines;
    }

    term_home();
    for(i = 0; i < termlines; i++) {
        term_clear_line();
        fputc('\n', stdout);
    }

    return(0);
}

int term_check_size() {
    int termlines, termcolumns;

    if(!termctx.only_print) {
        termlines = tigetnum("lines");
        termcolumns = tigetnum("columns");

        if(termlines != termctx.termlines ||
           termcolumns != termctx.termcolumns) {
            termctx.termlines = termlines;
            termctx.termcolumns = termcolumns;

            term_clear();
            term_home();

            return(1);
        }
    }

    return(0);
}

int term_print(const char *f, ...) {
    int n;
    va_list ap;
    int cleared;

    if(!termctx.only_print) {
        cleared = term_check_size();

        term_bottom();

        va_start(ap, f);
        n = vfprintf(stdout, f, ap);
        va_end(ap);

        if(!cleared) {
            if(term_clear_status_area() < 0) {
                termctx.only_print = 1;
                return(n);
            }
        }
        term_home();
        if(termctx.laststr != NULL) {
            fputs(termctx.laststr, stdout);
        }
    } else {
        va_start(ap, f);
        n = vfprintf(stdout, f, ap);
        va_end(ap);
        fputc('\n', stdout);
    }

    return(n);
}

int term_print_static(const char *f, ...) {
    int n;
    va_list ap;
    char *str;

    if(!termctx.only_print) {
        if(termctx.laststr != NULL) {
            /*
            if(term_clear_status_area() < 0) {
                termctx.only_print = 1;

                va_start(ap, f);
                n = vfprintf(stdout, f, ap);
                va_end(ap);

                return(n);
            }
            */

            free(termctx.laststr);
            termctx.laststr = NULL;
        }

        va_start(ap, f);
        termctx.lastlen = vsnprintf(NULL, 0, f, ap);
        va_end(ap);

        str = malloc(termctx.lastlen + 1);
        if(str == NULL) {
            return(-1);
        }

        va_start(ap, f);
        n = vsnprintf(str, termctx.lastlen, f, ap);
        va_end(ap);
        if(n != termctx.lastlen) {
            goto error;
        }

        term_home();
        fputs(str, stdout);

        termctx.laststr = str;
    } else {
        va_start(ap, f);
        n = vfprintf(stdout, f, ap);
        va_end(ap);
        fputc('\n', stdout);
    }

    return(n);

error:
    free(str);
    return(-1);
}
