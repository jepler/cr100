/*
 * Copyright (c) 2016 Julien Palard.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lw_terminal_vt100.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int get_mode_mask(unsigned int mode) {
    switch (mode) {
    case LNM:
        return MASK_LNM;
    case DECCKM:
        return MASK_DECCKM;
    case DECANM:
        return MASK_DECANM;
    case DECCOLM:
        return MASK_DECCOLM;
    case DECSCLM:
        return MASK_DECSCLM;
    case DECSCNM:
        return MASK_DECSCNM;
    case DECOM:
        return MASK_DECOM;
    case DECAWM:
        return MASK_DECAWM;
    case DECARM:
        return MASK_DECARM;
    case DECINLM:
        return MASK_DECINLM;
    default:
        return 0;
    }
}

/*
  Modes
  =====

  The following is a list of VT100 modes which may be changed with set
  mode (SM) and reset mode (RM) controls.

  ANSI Specified Modes
  --------------------

  Parameter    Mode Mnemonic    Mode Function
  0                             Error (ignored)
  20           LNM              Line feed new line mode


  DEC Private Modes
  =================
  If the first character in the parameter string is ? (077), the
  parameters are interpreted as DEC private parameters according to the
  following:

  Parameter    Mode Mnemonic    Mode Function
  0                             Error (ignored)
  1            DECCKM           Cursor key
  2            DECANM           ANSI/VT52
  3            DECCOLM          Column
  4            DECSCLM          Scrolling
  5            DECSCNM          Screen
  6            DECOM            Origin
  7            DECAWM           Auto wrap
  8            DECARM           Auto repeating
  9            DECINLM          Interlace

  LNM – Line Feed/New Line Mode
  -----------------------------
  This is a parameter applicable to set mode (SM) and reset mode (RM)
  control sequences. The reset state causes the interpretation of the
  line feed (LF), defined in ANSI Standard X3.4-1977, to imply only
  vertical movement of the active position and causes the RETURN key
  (CR) to send the single code CR. The set state causes the LF to imply
  movement to the first position of the following line and causes the
  RETURN key to send the two codes (CR, LF). This is the New Line (NL)
  option.

  This mode does not affect the index (IND), or next line (NEL) format
  effectors.

  DECCKM – Cursor Keys Mode (DEC Private)
  ---------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. This mode is only effective when the terminal
  is in keypad application mode (see DECKPAM) and the ANSI/VT52 mode
  (DECANM) is set (see DECANM). Under these conditions, if the cursor
  key mode is reset, the four cursor function keys will send ANSI cursor
  control commands. If cursor key mode is set, the four cursor function
  keys will send application functions.

  DECANM – ANSI/VT52 Mode (DEC Private)
  -------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes only VT52 compatible
  escape sequences to be interpreted and executed. The set state causes
  only ANSI "compatible" escape and control sequences to be interpreted
  and executed.

  DECCOLM – Column Mode (DEC Private)
  -----------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes a maximum of 80 columns
  on the screen. The set state causes a maximum of 132 columns on the
  screen.

  DECSCLM – Scrolling Mode (DEC Private)
  --------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes scrolls to "jump"
  instantaneously. The set state causes scrolls to be "smooth" at a
  maximum rate of six lines per second.

  DECSCNM – Screen Mode (DEC Private)
  -----------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes the screen to be black
  with white characters. The set state causes the screen to be white
  with black characters.

  DECOM – Origin Mode (DEC Private)
  ---------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes the origin to be at the
  upper-left character position on the screen. Line and column numbers
  are, therefore, independent of current margin settings. The cursor may
  be positioned outside the margins with a cursor position (CUP) or
  horizontal and vertical position (HVP) control.

  The set state causes the origin to be at the upper-left character
  position within the margins. Line and column numbers are therefore
  relative to the current margin settings. The cursor is not allowed to
  be positioned outside the margins.

  The cursor is moved to the new home position when this mode is set or
  reset.

  Lines and columns are numbered consecutively, with the origin being
  line 1, column 1.

  DECAWM – Autowrap Mode (DEC Private)
  ------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes any displayable
  characters received when the cursor is at the right margin to replace
  any previous characters there. The set state causes these characters
  to advance to the start of the next line, doing a scroll up if
  required and permitted.

  DECARM – Auto Repeat Mode (DEC Private)
  ---------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state causes no keyboard keys to
  auto-repeat. The set state causes certain keyboard keys to auto-repeat.

  DECINLM – Interlace Mode (DEC Private)
  --------------------------------------
  This is a private parameter applicable to set mode (SM) and reset mode
  (RM) control sequences. The reset state (non-interlace) causes the video
  processor to display 240 scan lines per frame. The set state (interlace)
  causes the video processor to display 480 scan lines per frame. There is
  no increase in character resolution.
*/

#define MOD1(a, b) ((a < b) ? a : a - b)

#define SCREEN_PTR(vt100, x, y)                                                \
    MOD1((vt100->top_line * vt100->width + x + vt100->width * y),              \
         (vt100->width * SCROLLBACK * vt100->height))

#define FROZEN_SCREEN_PTR(vt100, x, y)                                         \
    (MOD1(x + vt100->width * y, (vt100->width * SCROLLBACK * vt100->height)))

static lw_cell_t aget(struct lw_terminal_vt100 *headless_term, unsigned int x,
                      unsigned int y) {
    if (y < headless_term->margin_top || y > headless_term->margin_bottom)
        return headless_term
            ->afrozen_screen[FROZEN_SCREEN_PTR(headless_term, x, y)];
    else
        return headless_term->ascreen[SCREEN_PTR(headless_term, x, y)];
}

static void aset(struct lw_terminal_vt100 *headless_term, unsigned int x,
                 unsigned int y, lw_cell_t c) {
    if (y < headless_term->margin_top || y > headless_term->margin_bottom)
        headless_term->afrozen_screen[FROZEN_SCREEN_PTR(headless_term, x, y)] =
            c;
    else
        headless_term->ascreen[SCREEN_PTR(headless_term, x, y)] = c;
}

static void set(struct lw_terminal_vt100 *headless_term, unsigned int x,
                unsigned int y, char c) {
    aset(headless_term, x, y, (unsigned char)c | headless_term->attr);
}

lw_cell_t lw_terminal_vt100_aget(struct lw_terminal_vt100 *vt100,
                                 unsigned int x, unsigned int y) {
    if (y < vt100->margin_top || y > vt100->margin_bottom)
        return vt100->afrozen_screen[FROZEN_SCREEN_PTR(vt100, x, y)];
    else
        return vt100->ascreen[SCREEN_PTR(vt100, x, y)];
}

static void froze_line(struct lw_terminal_vt100 *vt100, unsigned int y) {
    memcpy(vt100->afrozen_screen + vt100->width * y,
           vt100->ascreen + SCREEN_PTR(vt100, 0, y),
           vt100->width * sizeof(lw_cell_t));
}

static void unfroze_line(struct lw_terminal_vt100 *vt100, unsigned int y) {
    memcpy(vt100->ascreen + SCREEN_PTR(vt100, 0, y),
           vt100->afrozen_screen + vt100->width * y,
           vt100->width * sizeof(lw_cell_t));
}

static void blank_screen(struct lw_terminal_vt100 *lw_terminal_vt100) {
    unsigned int x;
    unsigned int y;

    for (x = 0; x < lw_terminal_vt100->width; ++x)
        for (y = 0; y < lw_terminal_vt100->height; ++y)
            set(lw_terminal_vt100, x, y, ' ');
}

/*
  DECSC – Save Cursor (DEC Private)

  ESC 7

  This sequence causes the cursor position, graphic rendition, and
  character set to be saved. (See DECRC).
*/
static void DECSC(struct lw_terminal *term_emul) {
    /*TODO: Save graphic rendition and charset.*/
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    vt100->saved_x = vt100->x;
    vt100->saved_y = vt100->y;
}

/*
  RM – Reset Mode

  ESC [ Ps ; Ps ; . . . ; Ps l

  Resets one or more VT100 modes as specified by each selective
  parameter in the parameter string. Each mode to be reset is specified
  by a separate parameter. [See Set Mode (SM) control sequence]. (See
  Modes following this section).

*/
static void RM(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int mode;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (term_emul->argc > 0) {
        mode = term_emul->argv[0];
        if (mode == DECCOLM) {
            vt100->width = 80;
            vt100->x = vt100->y = 0;
            blank_screen(vt100);
        }
        UNSET_MODE(vt100, mode);
    }
}

/*
  SGR - Character attributes

  ESC [ Pm m

  Invoke the graphic rendition specified by the parameter(s). All
  following characters transmitted to the VT100 are rendered according
  to the parameter(s) until the next occurrence of SGR.

  [list from urxvt(7); x marks "implemented here"]

x Pm = 0             Normal (default)
x Pm = 1 / 21        On / Off Bold (bright fg)
  Pm = 3 / 23        On / Off Italic
  Pm = 4 / 24        On / Off Underline
x Pm = 5 / 25        On / Off Slow Blink (bright bg)
x Pm = 6 / 26        On / Off Rapid Blink (bright bg)

x Pm = 7 / 27        On / Off Inverse
  Pm = 8 / 27        On / Off Invisible
x Pm = 30 / 40       fg/bg Black
x Pm = 31 / 41       fg/bg Red
x Pm = 32 / 42       fg/bg Green
x Pm = 33 / 43       fg/bg Yellow
x Pm = 34 / 44       fg/bg Blue
x Pm = 35 / 45       fg/bg Magenta
x Pm = 36 / 46       fg/bg Cyan
x Pm = 37 / 47       fg/bg White
  Pm = 38;5 / 48;5   set fg/bg to colour #m (ISO 8613-6)
  Pm = 38;2;R;G;B    set fg to 24-bit colour #RGB (ISO 8613-3)
  Pm = 48;2;R;G;B    set bg to 24-bit colour #RGB (ISO 8613-3)
x Pm = 39 / 49       fg/bg Default
x Pm = 90 / 100      fg/bg Bright Black
x Pm = 91 / 101      fg/bg Bright Red
x Pm = 92 / 102      fg/bg Bright Green
x Pm = 93 / 103      fg/bg Bright Yellow
x Pm = 94 / 104      fg/bg Bright Blue
x Pm = 95 / 105      fg/bg Bright Magenta
x Pm = 96 / 106      fg/bg Bright Cyan
x Pm = 97 / 107      fg/bg Bright White
x Pm = 99 / 109      fg/bg Bright Default
*/

static int default_map_unicode(void *user_data, int c, lw_cell_t *attr) {
    (void)user_data;
    (void)c;
    (void)attr;
    return '?';
}

static void do_dummy_bell(void *user_data) {}

static lw_cell_t default_encode_attr(void *user_data,
                                     const struct lw_parsed_attr *attr) {
    (void)user_data;

    lw_cell_t result;
    if (attr->inverse) {
        result = attr->bg;
        result |= attr->fg << 4;
    } else {
        result = attr->fg;
        result |= attr->bg << 4;
    }
    if (attr->bold)
        result ^= 0x08;
    if (attr->blink)
        result ^= 0x80;

    return result << 8;
}

static void SGR(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100 =
        (struct lw_terminal_vt100 *)term_emul->user_data;

    // vim sends "CSI > 4 ; 2 m" and you can't stop it
    if (term_emul->flag == '>') {
        return;
    }

    if (term_emul->argc == 0) {
        term_emul->argc = 1;
        term_emul->argv[0] = 0;
    }

    for (unsigned int i = 0; i < term_emul->argc; i++) {
        int p = term_emul->argv[i];
        switch (p) {
        case 0:
            vt100->parsed_attr = LW_DEFAULT_ATTR;
            break;

        case 1:
            vt100->parsed_attr.bold = true;
            break;

        case 21:
            vt100->parsed_attr.bold = false;
            break;

        case 5:
        case 6:
            vt100->parsed_attr.blink = true;
            break;

        case 25:
        case 26:
            vt100->parsed_attr.blink = false;
            break;

        case 7:
            vt100->parsed_attr.inverse = true;
            break;

        case 27:
            vt100->parsed_attr.inverse = false;
            break;

        case 39:
            vt100->parsed_attr.fg = 7;
            break;

        case 49:
            vt100->parsed_attr.bg = 0;
            break;

        case 30 ... 37:
            vt100->parsed_attr.fg = p - 30;
            break;

        case 40 ... 47:
            vt100->parsed_attr.bg = p - 40;
            break;

        case 90 ... 97:
            vt100->parsed_attr.fg = p - 30 + 8;
            break;

        case 100 ... 107:
            vt100->parsed_attr.fg = p - 30 + 8;
            break;
        }
    }
    vt100->attr = vt100->encode_attr(vt100, &vt100->parsed_attr);
}

/*
  CUP – Cursor Position

  ESC [ Pn ; Pn H        default value: 1

  The CUP sequence moves the active position to the position specified
  by the parameters. This sequence has two parameter values, the first
  specifying the line position and the second specifying the column
  position. A parameter value of zero or one for the first or second
  parameter moves the active position to the first line or column in the
  display, respectively. The default condition with no parameters
  present is equivalent to a cursor to home action. In the VT100, this
  control behaves identically with its format effector counterpart,
  HVP. Editor Function

  The numbering of lines depends on the state of the Origin Mode
  (DECOM).
*/
static void CUP(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    int arg0;
    int arg1;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 0;
    arg1 = 0;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0] - 1;
    if (term_emul->argc > 1)
        arg1 = term_emul->argv[1] - 1;
    if (arg0 < 0)
        arg0 = 0;
    if (arg1 < 0)
        arg1 = 0;

    if (MODE_IS_SET(vt100, DECOM)) {
        arg0 += vt100->margin_top;
        if ((unsigned int)arg0 > vt100->margin_bottom)
            arg0 = vt100->margin_bottom;
    }
    if (arg0 >= (int)vt100->height) {
        arg0 = vt100->height - 1;
    }
    if (arg1 >= (int)vt100->width) {
        arg1 = vt100->width - 1;
    }
    vt100->y = arg0;
    vt100->x = arg1;
}

/*
  SM – Set Mode

  ESC [ Ps ; . . . ; Ps h

  Causes one or more modes to be set within the VT100 as specified by
  each selective parameter in the parameter string. Each mode to be set
  is specified by a separate parameter. A mode is considered set until
  it is reset by a reset mode (RM) control sequence.

*/
static void SM(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int mode;
    unsigned int saved_argc;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (term_emul->argc > 0) {
        mode = term_emul->argv[0];
        SET_MODE(vt100, mode);
        if (mode == DECANM) {
            /* TODO: Support vt52 mode */
            return;
        }
        if (mode == DECCOLM) {
            vt100->width = 132;
            vt100->x = vt100->y = 0;
            blank_screen(vt100);
        }
        if (mode == DECOM) {
            saved_argc = term_emul->argc;
            term_emul->argc = 0;
            CUP(term_emul);
            term_emul->argc = saved_argc;
        }
    }
}

/*
  DECSTBM – Set Top and Bottom Margins (DEC Private)

  ESC [ Pn; Pn r

  This sequence sets the top and bottom margins to define the scrolling
  region. The first parameter is the line number of the first line in
  the scrolling region; the second parameter is the line number of the
  bottom line in the scrolling region. Default is the entire screen (no
  margins). The minimum size of the scrolling region allowed is two
  lines, i.e., the top margin must be less than the bottom margin. The
  cursor is placed in the home position (see Origin Mode DECOM).

*/
static void DECSTBM(struct lw_terminal *term_emul) {
    unsigned int margin_top;
    unsigned int margin_bottom;
    struct lw_terminal_vt100 *vt100;
    unsigned int line;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;

    if (term_emul->argc == 2) {
        margin_top = term_emul->argv[0] - 1;
        margin_bottom = term_emul->argv[1] - 1;
        if (margin_bottom >= vt100->height)
            return;
        if (margin_bottom - margin_top <= 0)
            return;
    } else {
        margin_top = 0;
        margin_bottom = vt100->height - 1;
    }
    for (line = vt100->margin_top; line < margin_top; ++line)
        froze_line(vt100, line);
    for (line = vt100->margin_bottom; line < margin_bottom; ++line)
        unfroze_line(vt100, line);
    for (line = margin_top; line < vt100->margin_top; ++line)
        unfroze_line(vt100, line);
    for (line = margin_bottom; line < vt100->margin_bottom; ++line)
        froze_line(vt100, line);
    vt100->margin_bottom = margin_bottom;
    vt100->margin_top = margin_top;
    term_emul->argc = 0;
    CUP(term_emul);
}

/*
  DA – Device Attributes

  ESC [ Pn c


  The host requests the VT100 to send a device attributes (DA) control
  sequence to identify itself by sending the DA control sequence with
  either no parameter or a parameter of 0.  Response to the request
  described above (VT100 to host) is generated by the VT100 as a DA
  control sequence with the numeric parameters as follows:

  Option Present              Sequence Sent
  No options                  ESC [?1;0c
  Processor option (STP)      ESC [?1;1c
  Advanced video option (AVO) ESC [?1;2c
  AVO and STP                 ESC [?1;3c
  Graphics option (GPO)       ESC [?1;4c
  GPO and STP                 ESC [?1;5c
  GPO and AVO                 ESC [?1;6c
  GPO, STP and AVO            ESC [?1;7c

*/
static void DA(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    vt100->master_write(vt100->user_data, "\033[?1;0c", 7);
}

static void DSR(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;

    if (!term_emul->argc) {
        return;
    }

    switch (term_emul->argv[0]) {
    case 5:
        vt100->master_write(vt100->user_data, "\033[0n", 4);
        break;

    case 6: {
        char buf[16];
        snprintf(buf, sizeof(buf), "\033[%d;%dR", vt100->y + 1, vt100->x + 1);
        vt100->master_write(vt100->user_data, buf, strlen(buf));
    }

    default:;
    }
}

/*
  DECRC – Restore Cursor (DEC Private)

  ESC 8

  This sequence causes the previously saved cursor position, graphic
  rendition, and character set to be restored.
*/
static void DECRC(struct lw_terminal *term_emul) {
    /*TODO Save graphic rendition and charset */
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    vt100->x = vt100->saved_x;
    vt100->y = vt100->saved_y;
}

/*
  DECALN – Screen Alignment Display (DEC Private)

  ESC # 8

  This command fills the entire screen area with uppercase Es for screen
  focus and alignment. This command is used by DEC manufacturing and
  Field Service personnel.
*/
static void DECALN(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int x;
    unsigned int y;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    for (x = 0; x < vt100->width; ++x)
        for (y = 0; y < vt100->height; ++y)
            set(vt100, x, y, 'E');
}

/*
  IND – Index

  ESC D

  This sequence causes the active position to move downward one line
  without changing the column position. If the active position is at the
  bottom margin, a scroll up is performed. Format Effector
*/
static void IND(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int x;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (vt100->y >= vt100->margin_bottom) {
        /* SCROLL */
        vt100->top_line = (vt100->top_line + 1) % (vt100->height * SCROLLBACK);
        for (x = 0; x < vt100->width; ++x)
            set(vt100, x, vt100->margin_bottom, ' ');

    } else {
        /* Do not scroll, just move downward on the current display space */
        vt100->y += 1;
    }
}
/*
  RI – Reverse Index

  ESC M

  Move the active position to the same horizontal position on the
  preceding line. If the active position is at the top margin, a scroll
  down is performed. Format Effector
*/
static void RI(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (vt100->y == 0) {
        /* SCROLL */
        vt100->top_line = (vt100->top_line - 1) % (vt100->height * SCROLLBACK);
    } else {
        /* Do not scroll, just move upward on the current display space */
        vt100->y -= 1;
    }
}

/*
  NEL – Next Line

  ESC E

  This sequence causes the active position to move to the first position
  on the next line downward. If the active position is at the bottom
  margin, a scroll up is performed. Format Effector
*/
static void NEL(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int x;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (vt100->y >= vt100->margin_bottom) {
        /* SCROLL */
        vt100->top_line = (vt100->top_line + 1) % (vt100->height * SCROLLBACK);
        for (x = 0; x < vt100->width; ++x)
            set(vt100, x, vt100->margin_bottom, ' ');
    } else {
        /* Do not scroll, just move downward on the current display space */
        vt100->y += 1;
    }
    vt100->x = 0;
}

/*
  CUU – Cursor Up – Host to VT100 and VT100 to Host

  ESC [ Pn A        default value: 1

  Moves the active position upward without altering the column
  position. The number of lines moved is determined by the parameter. A
  parameter value of zero or one moves the active position one line
  upward. A parameter value of n moves the active position n lines
  upward. If an attempt is made to move the cursor above the top margin,
  the cursor stops at the top margin. Editor Function
*/
static void CUU(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 1;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0)
        arg0 = 1;
    if (arg0 <= vt100->y)
        vt100->y -= arg0;
    else
        vt100->y = 0;
}

/*
  CUD – Cursor Down – Host to VT100 and VT100 to Host

  ESC [ Pn B        default value: 1

  The CUD sequence moves the active position downward without altering
  the column position. The number of lines moved is determined by the
  parameter. If the parameter value is zero or one, the active position
  is moved one line downward. If the parameter value is n, the active
  position is moved n lines downward. In an attempt is made to move the
  cursor below the bottom margin, the cursor stops at the bottom
  margin. Editor Function
*/
static void CUD(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 1;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0)
        arg0 = 1;
    vt100->y += arg0;
    if (vt100->y >= vt100->height)
        vt100->y = vt100->height - 1;
}

/*
  CUF – Cursor Forward – Host to VT100 and VT100 to Host

  ESC [ Pn C        default value: 1

  The CUF sequence moves the active position to the right. The distance
  moved is determined by the parameter. A parameter value of zero or one
  moves the active position one position to the right. A parameter value
  of n moves the active position n positions to the right. If an attempt
  is made to move the cursor to the right of the right margin, the
  cursor stops at the right margin. Editor Function
*/
static void CUF(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 1;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0)
        arg0 = 1;
    vt100->x += arg0;
    if (vt100->x >= vt100->width)
        vt100->x = vt100->width - 1;
}

/*
  CUB – Cursor Backward – Host to VT100 and VT100 to Host

  ESC [ Pn D        default value: 1

  The CUB sequence moves the active position to the left. The distance
  moved is determined by the parameter. If the parameter value is zero
  or one, the active position is moved one position to the left. If the
  parameter value is n, the active position is moved n positions to the
  left. If an attempt is made to move the cursor to the left of the left
  margin, the cursor stops at the left margin. Editor Function
*/
static void CUB(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 1;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0)
        arg0 = 1;
    if (arg0 < vt100->x)
        vt100->x -= arg0;
    else
        vt100->x = 0;
}

/*
  DCH - Delete character(s) [default: 1]

  ESC [ Ps P        default value: 1

*/
static void DCH(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;
    unsigned int x;
    unsigned int y;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 1;
    y = vt100->y;

    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];

    for (x = vt100->x; x < vt100->width; ++x) {
        int x1 = x + arg0;
        if (x1 >= (int)vt100->width) {
            set(vt100, x, y, ' ');
        } else {
            aset(vt100, x, y, aget(vt100, x1, y));
        }
    }
}

/*
  ED – Erase In Display

  ESC [ Ps J        default value: 0

  This sequence erases some or all of the characters in the display
  according to the parameter. Any complete line erased by this sequence
  will return that line to single width mode. Editor Function

  Parameter Parameter Meaning
  0         Erase from the active position to the end of the screen,
  inclusive (default)
  1         Erase from start of the screen to the active position, inclusive
  2         Erase all of the display – all lines are erased, changed to
  single-width, and the cursor does not move.
*/
static void ED(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;
    unsigned int x;
    unsigned int y;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 0;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0) {
        for (x = vt100->x; x < vt100->width; ++x)
            set(vt100, x, vt100->y, ' ');
        for (x = 0; x < vt100->width; ++x)
            for (y = vt100->y + 1; y < vt100->height; ++y)
                set(vt100, x, y, ' ');
    } else if (arg0 == 1) {
        for (x = 0; x < vt100->width; ++x)
            for (y = 0; y < vt100->y; ++y)
                set(vt100, x, y, ' ');
        for (x = 0; x <= vt100->x; ++x)
            set(vt100, x, vt100->y, ' ');
    } else if (arg0 == 2) {
        for (x = 0; x < vt100->width; ++x)
            for (y = 0; y < vt100->height; ++y)
                set(vt100, x, y, ' ');
    }
}

/*
  EL – Erase In Line

  ESC [ Ps K        default value: 0

  Erases some or all characters in the active line according to the
  parameter. Editor Function

  Parameter Parameter Meaning
  0         Erase from the active position to the end of the line, inclusive
  (default)
  1         Erase from the start of the screen to the active position, inclusive
  2         Erase all of the line, inclusive
*/
static void EL(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int arg0;
    unsigned int x;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    arg0 = 0;
    if (term_emul->argc > 0)
        arg0 = term_emul->argv[0];
    if (arg0 == 0) {
        for (x = vt100->x; x < vt100->width; ++x)
            set(vt100, x, vt100->y, ' ');
    } else if (arg0 == 1) {
        for (x = 0; x <= vt100->x; ++x)
            set(vt100, x, vt100->y, ' ');
    } else if (arg0 == 2) {
        for (x = 0; x < vt100->width; ++x)
            set(vt100, x, vt100->y, ' ');
    }
}

/*
  HVP – Horizontal and Vertical Position

  ESC [ Pn ; Pn f        default value: 1

  Moves the active position to the position specified by the
  parameters. This sequence has two parameter values, the first
  specifying the line position and the second specifying the column. A
  parameter value of either zero or one causes the active position to
  move to the first line or column in the display, respectively. The
  default condition with no parameters present moves the active position
  to the home position. In the VT100, this control behaves identically
  with its editor function counterpart, CUP. The numbering of lines and
  columns depends on the reset or set state of the origin mode
  (DECOM). Format Effector
*/
static void HVP(struct lw_terminal *term_emul) { CUP(term_emul); }

static void TBC(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;
    unsigned int i;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    if (term_emul->argc == 0 || term_emul->argv[0] == 0) {
        vt100->tabulations[vt100->x] = '-';
    } else if (term_emul->argc == 1 && term_emul->argv[0] == 3) {
        for (i = 0; i < 132; ++i)
            vt100->tabulations[i] = '-';
    }
}

static void HTS(struct lw_terminal *term_emul) {
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;
    vt100->tabulations[vt100->x] = '|';
}

static void vt100_write_unicode(struct lw_terminal *term_emul, int c) {
    struct lw_terminal_vt100 *vt100;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;

    if (c == '\r') {
        vt100->x = 0;
        return;
    }
    if (c == '\7') {
        vt100->do_bell(vt100);
        return;
    }
    if (c == '\n' || c == '\013' || c == '\014') {
        if (MODE_IS_SET(vt100, LNM))
            NEL(term_emul);
        else
            IND(term_emul);
        return;
    }
    if (c == '\010' && vt100->x > 0) {
        if (vt100->x == vt100->width)
            vt100->x -= 1;
        vt100->x -= 1;
        return;
    }
    if (c == '\t') {
        do {
            set(vt100, vt100->x, vt100->y, ' ');
            vt100->x += 1;
        } while (vt100->x < vt100->width &&
                 vt100->tabulations[vt100->x] == '-');
        return;
    }
    if (c == '\016') {
        vt100->selected_charset = 0;
        return;
    }
    if (c == '\017') {
        vt100->selected_charset = 1;
        return;
    }
    if (c < ' ') {
        return;
    }

    lw_cell_t attr = vt100->attr;

    if (vt100->x == vt100->width) {
        if (MODE_IS_SET(vt100, DECAWM))
            NEL(term_emul);
        else
            vt100->x -= 1;
    }
    if (!vt100->selected_charset && c > 95 && c < 127) {
        c = c - 95; // you can't hit the glyph at 0 this way, oh well
    }
    if (!vt100->unicode && !vt100->selected_charset && c > 32 && c <= 95) {
        // extension: In the alternate character set, there are also
        // sixels/sextant chars. Because there's only room for 32 of the 64
        // in the character bitmap (at 128-160) half are displayed in
        // inverse video instead.
        c = c + 64;
        if (c >= 160) {
            struct lw_parsed_attr tmp_attr = vt100->parsed_attr;
            tmp_attr.inverse = !tmp_attr.inverse;
            c ^= 0x1f;
            attr = vt100->encode_attr(vt100, &tmp_attr);
        }
    }
    if (c >= 0x100) {
        c = vt100->map_unicode(vt100, c, &attr);
    }
    aset(vt100, vt100->x, vt100->y, c | attr);
    vt100->x += 1;
}

#define REPLACEMENT ('?')

static void vt100_write(struct lw_terminal *term_emul, char c) {
    struct lw_terminal_vt100 *vt100;
    uint8_t uc = c;

    vt100 = (struct lw_terminal_vt100 *)term_emul->user_data;

    if (vt100->unicode) {
        if (vt100->ustate == 0) {
            if (uc < 0x80) {
                vt100_write_unicode(term_emul, uc);
            } else if ((uc & 0xe0) == 0xc0) {
                vt100->ustate = 1;
                vt100->ubits = (uc & 0x1f);
            } else if ((uc & 0xf0) == 0xe0) {
                vt100->ustate = 2;
                vt100->ubits = (uc & 0xf);
            } else if ((uc & 0xf8) == 0xf0) {
                vt100->ustate = 3;
                vt100->ubits = (uc & 0x7);
            } else {
                vt100_write_unicode(term_emul, REPLACEMENT);
            }
        } else {
            if (uc < 0x80) {
                vt100_write_unicode(term_emul, REPLACEMENT);
                vt100_write_unicode(term_emul, uc);
                vt100->ustate = 0;
            } else if ((uc & 0xc0) != 0x80) {
                vt100_write_unicode(term_emul, REPLACEMENT);
                vt100->ustate = 0;
            } else {
                vt100->ubits <<= 6;
                vt100->ubits |= uc & 0x3f;
                if (--vt100->ustate == 0) {
                    vt100_write_unicode(term_emul, vt100->ubits);
                }
            }
        }
    } else {
        vt100_write_unicode(term_emul, uc);
    }
}

#if defined(PICO_BUILD)
#include "pico.h"
#else
#define __not_in_flash_func(x) x
#endif
const lw_cell_t *
__not_in_flash_func(lw_terminal_vt100_getline)(struct lw_terminal_vt100 *vt100,
                                               unsigned int y) {
    if (y < vt100->margin_top || y > vt100->margin_bottom)
        return vt100->afrozen_screen + FROZEN_SCREEN_PTR(vt100, 0, y);
    else
        return vt100->ascreen + SCREEN_PTR(vt100, 0, y);
}

const lw_cell_t **lw_terminal_vt100_getlines(struct lw_terminal_vt100 *vt100) {
    unsigned int y;

    for (y = 0; y < vt100->height; ++y)
        vt100->alines[y] = lw_terminal_vt100_getline(vt100, y);
    return vt100->alines;
}

static void setcells(lw_cell_t *buf, lw_cell_t c, size_t n) {
    for (; n--; buf++)
        *buf = c;
}

struct lw_terminal_vt100 *lw_terminal_vt100_init(
    void *user_data,
    void (*unimplemented)(struct lw_terminal *term_emul, char *seq, char chr),
    void (*master_write)(void *user_data, void *buffer, size_t len),
    lw_cell_t (*encode_attr)(void *user_data,
                             const struct lw_parsed_attr *attr),
    unsigned int width, unsigned int height) {
    struct lw_terminal_vt100 *this;

    this = calloc(1, sizeof(*this));
    if (this == NULL)
        return NULL;
    this->user_data = user_data;
    this->height = height;
    this->width = width;
    this->ascreen = malloc(132 * SCROLLBACK * this->height * sizeof(lw_cell_t));
    if (this->ascreen == NULL)
        goto free_this;
    this->afrozen_screen = malloc(132 * this->height * sizeof(lw_cell_t));
    if (this->afrozen_screen == NULL)
        goto free_screen;
    this->tabulations = malloc(132);
    if (this->tabulations == NULL)
        goto free_frozen_screen;
    for (int i = 0; i < 132; i++) {
        this->tabulations[i] = (i && i % 8 == 0) ? '|' : '-';
    }
    this->margin_top = 0;
    this->margin_bottom = this->height - 1;
    this->selected_charset = 1;
    this->x = 0;
    this->y = 0;
    this->modes = MASK_DECANM;
    this->top_line = 0;
    this->lw_terminal = lw_terminal_parser_init();
    if (this->lw_terminal == NULL)
        goto free_tabulations;
    this->lw_terminal->user_data = this;
    this->lw_terminal->write = vt100_write;
    this->lw_terminal->callbacks.csi.f = HVP;
    this->lw_terminal->callbacks.csi.K = EL;
    this->lw_terminal->callbacks.csi.c = DA;
    this->lw_terminal->callbacks.csi.h = SM;
    this->lw_terminal->callbacks.csi.l = RM;
    this->lw_terminal->callbacks.csi.n = DSR;
    this->lw_terminal->callbacks.csi.J = ED;
    this->lw_terminal->callbacks.csi.H = CUP;
    this->lw_terminal->callbacks.csi.C = CUF;
    this->lw_terminal->callbacks.csi.B = CUD;
    this->lw_terminal->callbacks.csi.r = DECSTBM;
    this->lw_terminal->callbacks.csi.m = SGR;
    this->lw_terminal->callbacks.csi.A = CUU;
    this->lw_terminal->callbacks.csi.P = DCH;
    this->lw_terminal->callbacks.csi.g = TBC;
    this->lw_terminal->callbacks.esc.H = HTS;
    this->lw_terminal->callbacks.csi.D = CUB;
    this->lw_terminal->callbacks.esc.E = NEL;
    this->lw_terminal->callbacks.esc.D = IND;
    this->lw_terminal->callbacks.esc.M = RI;
    this->lw_terminal->callbacks.esc.n8 = DECRC;
    this->lw_terminal->callbacks.esc.n7 = DECSC;
    this->lw_terminal->callbacks.hash.n8 = DECALN;
    this->lw_terminal->unimplemented = unimplemented;
    this->master_write = master_write;
    this->encode_attr = encode_attr ? encode_attr : default_encode_attr;
    this->do_bell = do_dummy_bell;
    this->map_unicode = default_map_unicode;
    lw_terminal_vt100_read_str(this,
                               "\033[m\033[?7h"); // set default attributes
    setcells(this->ascreen, ' ' | this->attr, 132 * SCROLLBACK * this->height);
    setcells(this->afrozen_screen, ' ' | this->attr, 132 * this->height);
    return this;
free_tabulations:
    free(this->tabulations);
free_frozen_screen:
    free(this->afrozen_screen);
free_screen:
    free(this->ascreen);
free_this:
    free(this);
    return NULL;
}

#define CURSOR_ATTR (7 << 11) // very specific to cr100
static void show_cursor(struct lw_terminal_vt100 *this) {
    unsigned x = this->x, y = this->y;
    lw_cell_t cell;
    if (x == this->width)
        x -= 1;
    if (x >= this->width || y >= this->height) {
        this->cursor_saved_flag = false;
        return;
    }

    cell = aget(this, x, y);
    this->cursor_saved_x = x;
    this->cursor_saved_y = y;
    this->cursor_saved_flag = true;
    aset(this, x, y, cell ^ CURSOR_ATTR);
}

static void hide_cursor(struct lw_terminal_vt100 *this) {
    if (!this->cursor_saved_flag) {
        return;
    }
    this->cursor_saved_flag = false;
    unsigned x = this->cursor_saved_x, y = this->cursor_saved_y;
    aset(this, x, y, aget(this, x, y) ^ CURSOR_ATTR);
}

void lw_terminal_vt100_read_str(struct lw_terminal_vt100 *this,
                                const char *buffer) {
    lw_terminal_vt100_read_buf(this, buffer, strlen(buffer));
}

void lw_terminal_vt100_read_buf(struct lw_terminal_vt100 *this,
                                const char *buffer, size_t len) {
    hide_cursor(this);
    lw_terminal_parser_read_buf(this->lw_terminal, buffer, len);
    show_cursor(this);
}

void lw_terminal_vt100_destroy(struct lw_terminal_vt100 *this) {
    lw_terminal_parser_destroy(this->lw_terminal);
    free(this->tabulations);
    free(this->ascreen);
    free(this->afrozen_screen);
    free(this);
}
