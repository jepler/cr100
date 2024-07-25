#pragma once  

typedef enum {
    PRINTABLE,
    NO_OUTPUT,
    BELL,
    CLEAR_EOL,
    CLEAR_SCREEN,
    CURSOR_LEFT,
    CURSOR_POSITION,
    CHAR_ATTR
} vt_action;

typedef enum { NORMAL, ESC, CSI, P2, TITLE_PRE, TITLE, TITLE_END } vt_esc_st;

typedef struct { 
    vt_esc_st esc_st;
    int esc_param[2];
} esc_state;

void vt_init_state(esc_state *st);
vt_action vt_process_code(esc_state *st, int c);
