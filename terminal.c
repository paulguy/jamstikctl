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
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <curses.h>
#include <wchar.h>
/*
#include <termios.h>
#include <term.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
*/

typedef struct {
    WINDOW *main_term;
    WINDOW *notes_term;
    WINDOW *status_term;

    int lastlines;
} terminal_ctx_t;

terminal_ctx_t termctx;

int term_count_lines(int textwidth, int n, const char *str) {
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

        if(c == '\0') {
            break;
        }

        if(strlines == 0) {
            /* an empty string should be 0 lines, but at least 1 decoded
             * character implies at least 1 line. */
            strlines = 1;
        }

        if(c == '\n') {
            strlines++;
            strcolumns = 0;
            continue;
        }

        width = wcwidth(c);

        strcolumns += width;
        /* don't put a new line if the text string just fits the width */
        if(strcolumns == textwidth && i < n) {
            strlines++;
            strcolumns = 0;
        } else if(strcolumns > textwidth) {
            strlines++;
            strcolumns = width;
        }
    }

    return(strlines);
}

char *term_get_string_and_lines(int *lines, int *len,
                                const char *f, va_list ap) {
    char *str;
    va_list ap2;
    int n;

    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, f, ap2);
    va_end(ap2);

    str = malloc(n + 1);
    if(str == NULL) {
        return(NULL);
    }

    va_copy(ap2, ap);
    *len = vsnprintf(str, n+1, f, ap2);
    va_end(ap2);
    if(*len != n) {
        free(str);
        return(NULL);
    }

    *lines = term_count_lines(COLS, n, str);
    if(*lines <= 0) {
        free(str);
        return(NULL);
    }
    
    return(str);
}

void term_cleanup() {
    if(termctx.main_term != NULL) {
        termctx.main_term = NULL;
        delwin(termctx.status_term);
        termctx.status_term = NULL;
        delwin(termctx.notes_term);
        termctx.notes_term = NULL;
        endwin();
    }
}

int term_setup(int only_print) {
    termctx.main_term = NULL;

    if(only_print) {
        termctx.main_term = initscr();
        if(termctx.main_term == NULL) {
            goto error;
        }
        /* some recommended otpions I guess */
        cbreak();
        noecho();
        intrflush(termctx.main_term, FALSE);
        /* don't want delays */
        nodelay(termctx.main_term, TRUE);

        termctx.status_term = newwin(LINES, COLS, 1, 0);
        if(termctx.status_term == NULL) {
            goto error_main_term;
        }
        idlok(termctx.status_term, TRUE);
        scrollok(termctx.status_term, TRUE);
        termctx.notes_term = newwin(1, COLS, 0, 0);
        if(termctx.notes_term == NULL) {
            goto error_status_term;
        }
        termctx.lastlines = 1;
    }

    return(0);

error_status_term:
    delwin(termctx.status_term);
    termctx.status_term = NULL;
error_main_term:
    endwin();
    termctx.main_term = NULL;
error:
    return(-1);
}

int term_print_mode() {
    return(termctx.main_term == NULL);
}

int term_getkey() {
    int key;

    key = getch();
    
    if(key == ERR) {
        return(-1);
    }

    return(key);
}

int term_print(const char *f, ...) {
    int n;
    int strlines;
    va_list ap;
    char *str;

    if(term_print_mode()) {
        va_start(ap, f);
        n = vfprintf(stdout, f, ap);
        va_end(ap);
        fputc('\n', stdout);
    } else {
        /* push messages from the bottom */
        va_start(ap, f);
        str = term_get_string_and_lines(&strlines, &n, f, ap);
        va_end(ap);

        if(str == NULL) {
            return(-1);
        }

        wscrl(termctx.status_term, strlines);
        mvwaddnstr(termctx.status_term, LINES-termctx.lastlines-strlines, 0,
                   str, n);
        free(str);

        wrefresh(termctx.status_term);
    }

    return(n);
}

int term_print_static(const char *f, ...) {
    int n;
    va_list ap;
    char *str;
    int strlines;

    if(term_print_mode()) {
        va_start(ap, f);
        n = vfprintf(stdout, f, ap);
        va_end(ap);
        fputc('\n', stdout);
    } else {
        va_start(ap, f);
        str = term_get_string_and_lines(&strlines, &n, f, ap);
        va_end(ap);

        if(str == NULL) {
            return(-1);
        }

        if(strlines != termctx.lastlines) {
            if(strlines > termctx.lastlines) {
                wscrl(termctx.status_term, strlines - termctx.lastlines);
            }
            wresize(termctx.notes_term, strlines, COLS);
            wresize(termctx.status_term, LINES - strlines, COLS);
            mvwin(termctx.status_term, strlines, 0);
            if(strlines < termctx.lastlines) {
                wscrl(termctx.status_term, termctx.lastlines - strlines);
            }
            wrefresh(termctx.status_term);

            termctx.lastlines = strlines;
        }
        wclear(termctx.notes_term);

        mvwaddnstr(termctx.notes_term, 0, 0, str, n);
        free(str);

        wrefresh(termctx.notes_term);
    }

    return(n);
}
