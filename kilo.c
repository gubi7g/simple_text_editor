/* includes */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/* defines */

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

// STDIN_FILENO e din unistd.h

/* data */

struct editorConfig {
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

/* terminal */

void die(const char *s){
  write(STDOUT_FILENO, "\x1b[2J", 4); // clear fullscreen
  write(STDOUT_FILENO, "\x1b[H", 3); // cursor in top-left

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);// IXON = soft. flow control, ICRNL = input Carriage Return, New Line. 
  // BRKINT = break condition.
  // INPCK = parity check 
  // ISTRIP = causes the 8th bit to be strippepd
  // CS8 = not a flag, a bit maask. sets char size to 8 bits per byte.
 
  raw.c_oflag &= ~(OPOST);// Output Post-Processing.
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);// astia sunt toti flags
  raw.c_cc[VMIN] =  0;
  raw.c_cc[VTIME] =  1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read"); // EAGAIN = no data available. EAGAIN is not a read error here.
  }
  return c;
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32]; // buffer
  unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // n = Device Status Report. Argument 6 for cursor position
  
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }

  buf[i] = '\0';  


  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // C = Cursor Forward Command, B = Cursor Down Command. 999 is a large enough number.
    editorReadKey();
    return getCursorPosition(rows,cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
    }
}

/* append buffer */

struct abuf {
  char *b;
  int len
};

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len +len);

  if(new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

#define ABUF_INIT {NULL, 0}

/* input */

void editorProcessKeypress() {
  char c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'): // CTRL+Q to quit
      write(STDOUT_FILENO, "\x1b[2J", 4); //
      write(STDOUT_FILENO, "\x1b[H", 3);
  
      exit(0);
      break;
  }
}

/* output */

void editorDrawRows(struct abuf *ab) {
// draw tildes

  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
      if (welcomelen > E.screencols) welcomelen = E.screencols;
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while(padding--) abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else
      abAppend(ab, "~", 1);

    abAppend(ab, "\x1b[K", 3); // K = Erase in Line Command. Argument 2 = whole line, 1 = left of the line, 0 = right of the line. 0 is default (our option).
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;
  
  abAppend(&ab, "\x1b[?25l", 6); // show/hide cursor with the same command.
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25l", 6);

  write(STDOUT_FILENO, ab.b, ab.len); // J = erase in display command. \x1b[ is the escape sequence (27 in decimal). 2 is the argument of J command, meaning clear all screen.
  abFree(&ab); // H = cursor reposition command. Default arguments are 1;1, so it places the cursor in the top-left position.
}
/* init */

void initEditor() {
// get window size.
  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
} 

int main() {
	enableRawMode();
  initEditor();
	
  while(1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

	return 0;
}
