#include "vt.h"
#include <string.h>

void vt_init_state(esc_state *st) {
    memset(st, 0, sizeof(*st));
}

static vt_action csi_common(esc_state *st, int c, int i) {
  if (c >= '0' && c <= '9') {
    st->esc_param[i] = st->esc_param[i] * 10 + c - '0';
    return NO_OUTPUT;
  } 
  st->esc_st = NORMAL;
  if (c == 'K') {
    return CLEAR_EOL;
  } else if (c == 'J') {
    if (st->esc_param[0] == 2) {
      return CLEAR_SCREEN;
    }
  } else if (c == 'D') {
    return CURSOR_LEFT;
  } else if (c == 'H') {
    return CURSOR_POSITION;
  } else if (c == 'm') {
    return CHAR_ATTR;
  }
  return NO_OUTPUT;
}

vt_action vt_process_code(esc_state *st, int c) {
  switch (st->esc_st) {
  default:
  case NORMAL:
    if (c == 27) {
      st->esc_st = ESC;
      return NO_OUTPUT;
    }
    if (c == 7) {
      // bell
      return BELL;
    }
    if (c == 8) {
      st->esc_param[0] = 1;
      return CURSOR_LEFT;
    }
    return PRINTABLE;

  case ESC:
    if (c == '[') {
      st->esc_st = CSI;
      st->esc_param[0] = st->esc_param[1] = 0;
    } else if (c == ']') {
      st->esc_st = TITLE_PRE;
    } else {
      st->esc_st = NORMAL;
    }
    return NO_OUTPUT;

  case CSI:
    if (c == ';') {
      st->esc_st = P2;
      return NO_OUTPUT;
    } else {
      return csi_common(st, c, 0);
    }

  case P2:
    return csi_common(st, c, 1);
      
  case TITLE_PRE:
    if (c == ';') {
      st->esc_st = TITLE;
      return NO_OUTPUT;
    }   
      
  case TITLE:
    if (c == 0x1b) {
      st->esc_st = TITLE_END; 
    } 
    return NO_OUTPUT;
    
  case TITLE_END:
    if (c == 0x5c) {
      st->esc_st = NORMAL;
    } else {
      st->esc_st = TITLE;
    }
    return NO_OUTPUT;
  }   
}   

