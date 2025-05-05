#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <termios.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <locale.h>
#include "mtwister.h"

typedef struct _xyzw_int {
  int x, y, z, w;
} xyzw_int;

typedef struct _xy_int {
  int x, y;
} xy_int;

typedef struct _grid {
  xyzw_int size;
  xyzw_int cursor;
  unsigned long len;
  unsigned long uncovered;
  unsigned long bombs;
  unsigned long flagged;
  unsigned int seed;
  unsigned short display_width;
  int state;
  time_t start;
  time_t paused;
  time_t offset;
  unsigned int recursion_depth;
  short* mask;
  short* delta;
  short grid[];
} grid;

typedef struct _timer {
  xy_int pos;
  grid* g;
} timer;

typedef struct _option {
  short enabled;
  int value;
  int depth;
  int type;
  char name[15];
} option;

enum {
  CONTINUE = 0,
  STOP,
};

enum {
  BOOL = 0,
  UINT,
  EMPTY,
  DONE,
};

enum {
  BOMB = -1,
  UNCOVERED, //part of mask
  COVERED,
  FLAG_UNCOVERED,
  FLAG_COVERED,
};

enum {
  RUNNING = 0,
  PAUSED,
  CLICKED_BOMB,
  GAVE_UP,
  REVEAL_FIELD,
  WIN,
};

short COVERED_LIGHT[3] = {0x66, 0x66, 0x66};
short COVERED_DARK[3] = {0x3b, 0x3b, 0x3b};
short UNCOVERED_LIGHT[3] = {0xc6, 0xc6, 0xc6};
short UNCOVERED_DARK[3] = {0xb8, 0xb8, 0xb8};
short PAUAED_LIGHT[3] = {51, 51, 51};
short PAUSED_DARK[3] = {38, 38, 38};
short BLACK[3] = {0, 0, 0};
short PINK[3] = {255, 42, 255};
short COVERED_PINK_LIGHT[3] = {128, 115, 128};
short COVERED_PINK_DARK[3] = {89, 80, 89};
short UNCOVERED_PINK_LIGHT[3] = {179, 161, 179};
short UNCOVERED_PINK_DARK[3] = {166, 149, 166};
short PAUSED_PINK_LIGHT[3] = {77, 69, 77};
short PAUSED_PINK_DARK[3] = {64, 57, 64};
short RED[3] = {255, 0, 0};

int option_offset = 20;
int numbuffer_len = 10;

struct termios saved_attributes;

void
reset_input_mode (void)
{
  tcsetattr (STDIN_FILENO, TCSANOW, &saved_attributes);
}

void
set_input_mode (void)
{
  struct termios tattr;
  char *name;

  /* Make sure stdin is a terminal. */
  if (!isatty (STDIN_FILENO))
    {
      fprintf (stderr, "Not a terminal.\n");
      exit (EXIT_FAILURE);
    }

  /* Save the terminal attributes so we can restore them later. */
  tcgetattr (STDIN_FILENO, &saved_attributes);
  atexit (reset_input_mode);

  /* Set the funny terminal modes. */
  tcgetattr (STDIN_FILENO, &tattr);
  tattr.c_lflag &= ~(ICANON|ECHO); /* Clear ICANON and ECHO. */
  tattr.c_cc[VMIN] = 1;
  tattr.c_cc[VTIME] = 0;
  tcsetattr (STDIN_FILENO, TCSAFLUSH, &tattr);
}

void exit_game() {
  printf("\e[0m");
  printf("\e[?47l");
  printf("\e[?25h");
  printf("\e[u");
}

unsigned long get_size(xyzw_int size) {
  return size.x*size.y*size.z*size.w;
}

void print_xyzw(xyzw_int p) {
  fprintf(stderr, "xyzw_int:\n\tx: %d\n\ty: %d\n\tz: %d\n\ta: %d\n", p.x, p.y, p.z, p.w);
}

uint32_t rand_between(MTRand r, int min, int max) {
  return genRandLong(&r)%(max-min)+min;
}

xyzw_int rand_coord(grid* g, MTRand r) {
  xyzw_int p;
  p.x = genRand(&r)*g->size.x;
  p.y = genRand(&r)*g->size.y;
  p.z = genRand(&r)*g->size.z;
  p.w = genRand(&r)*g->size.w;
}

unsigned long grid_pos(grid* g, xyzw_int p) {
  return ((p.x+(p.y*g->size.x))*g->size.z+p.z)*g->size.w+p.w;
}

short grid_at(grid* g, xyzw_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    unsigned long pos = grid_pos(g, p);
    return g->grid[pos];
  } else {
    return -2;
  }
}

void grid_place_at(grid* g, xyzw_int p, short v) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    unsigned long pos = grid_pos(g, p);
    g->grid[pos] = v;
    int numwidth = log10(v);
    if (numwidth > 1) {
      g->display_width = numwidth+1;
    }
  }
}

void grid_inc_if_above_at(grid* g, xyzw_int p, short threshold) {
  unsigned long pos = grid_pos(g, p);
  if (g->grid[pos] > threshold) {
    g->grid[pos] += 1;
    int numwidth = log10(g->grid[pos]);
    if (numwidth > 1) {
      g->display_width = numwidth+1;
    }
  }
}

xy_int find_biggest_number(grid* g) {
  int bn = 0;
  int sn = 0;
  for (int w = 0; w < g->size.w; w++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzw_int p = {x, y, z, w};
          unsigned long pos = grid_pos(g, p);
          if (g->grid[pos] > bn) {
            bn = g->grid[pos];
          }
          if (g->delta[pos] < sn && g->delta[pos] < 0 && g->grid[pos] != BOMB) {
            sn = g->delta[pos];
          }
        }
      }
    }
  }
  bn = log10(bn)+1;
  sn = log10(-sn)+2;
  return (xy_int){(bn < 2)?2:bn, (sn < 2)?2:sn};
}

char* save_game_to_file(grid* g) {
  char filename = malloc(34);
  struct tm* tm_info = localtime(&g->start);
  strftime(filename, 34, "%Y-%m-%d_%H:%M:%S.4dminesweeper", tm_info);
  fprintf(stderr, "%s\n", filename);
  FILE* f = fopen(filename, "w");
  if (f == NULL) {
    fprintf(stderr, "Couldn't create file: %s\n", filename);
    sprintf(filename, "NO_FILE\0");
    return filename;
  } else {
    fprintf(stderr, "Opening file: %s\n", filename);
  }
  fprintf(f, "Save file for game run on %s\n", ctime(&g->start));
  fprintf(f, "Size:                         %d %d %d %d\n", g->size.x, g->size.y, g->size.z, g->size.w);
  fprintf(f, "Cursor:                       %d %d %d %d\n", g->cursor.x, g->cursor.y, g->cursor.z, g->cursor.w);
  fprintf(f, "Len:                          %lu\n", g->len);
  fprintf(f, "Uncovered:                    %lu\n", g->uncovered);
  fprintf(f, "Bombs:                        %lu\n", g->bombs);
  fprintf(f, "Flagged:                      %lu\n", g->flagged);
  fprintf(f, "Seed:                         %u\n", g->seed);
  fprintf(f, "Display width:                %u\n", g->display_width);
  fprintf(f, "State:                        ");
  switch (g->state) {
    case RUNNING:
      fprintf(f, "Running\n");
      break;
    case PAUSED:
      fprintf(f, "Paused\n");
      break;
    case CLICKED_BOMB:
      fprintf(f, "Clicked bomb\n");
      break;
    case GAVE_UP:
      fprintf(f, "Gave up\n");
      break;
    case REVEAL_FIELD:
      fprintf(f, "Reveal field\n");
      break;
    case WIN:
      fprintf(f, "Win\n");
      break;
    default:
      fprintf(f, "Unkown\n");
  }
  filename[19] = '\0';
  fprintf(f, "Started:                      %s\n", filename);
  tm_info = localtime(&g->offset);
  strftime(filename, 20, "%Y-%m-%d_%H:%M:%S", tm_info);
  fprintf(f, "Offset:                       %s\n", filename);
  fprintf(f, "Recursion depth:              %u\n\n", g->recursion_depth);

  xy_int bn = find_biggest_number(g);
  short display_width = (bn.x > bn.y)?bn.x:bn.y;

  unsigned long buffer_size =
    g->len*(display_width+1) //storage for the numbers plus ','
    +((g->size.z-1)*g->size.y*g->size.w*2) //storage for vertical lines: '| '
    +((g->size.w-1)*((g->size.x*(display_width+1)+2)*g->size.z)); //storage for horizontal line: '-+-'
  char* grid = malloc(buffer_size);
  char* mask = malloc(buffer_size);
  char* delta = malloc(buffer_size);
  for (unsigned long i = 0; i < buffer_size-1; i++) {
    grid[i] = ' ';
    mask[i] = ' ';
    delta[i] = ' ';
  }
  unsigned long buffer_offset = 0;

  unsigned long pos;
  short grid_value;
  short mask_value;
  short delta_value;
  int grid_writen;
  int mask_writen;
  int delta_writen;

  for (int w = 0; w < g->size.w; w++) {
    for (int y = 0; y < g->size.y; y++) {
      for (int z = 0; z < g->size.z; z++) {
        for (int x = 0; x < g->size.x-1; x++) {
          pos = grid_pos(g, (xyzw_int){x, y, z, w});
          grid_value = g->grid[pos];
          mask_value = g->mask[pos];
          delta_value = g->delta[pos];
          grid_writen = sprintf(&grid[buffer_offset], "%d,", grid_value);
          grid[buffer_offset+grid_writen] = ' ';
          mask_writen = sprintf(&mask[buffer_offset], "%d,", mask_value);
          mask[buffer_offset+mask_writen] = ' ';
          delta_writen = sprintf(&delta[buffer_offset], "%d,", delta_value);
          delta[buffer_offset+delta_writen] = ' ';
          buffer_offset += display_width+1;
        }
        pos = grid_pos(g, (xyzw_int){g->size.x-1, y, z, w});
        grid_value = g->grid[pos];
        mask_value = g->mask[pos];
        delta_value = g->delta[pos];
        grid_writen = sprintf(&grid[buffer_offset], "%d", grid_value);
        grid[buffer_offset+grid_writen] = ' ';
        mask_writen = sprintf(&mask[buffer_offset], "%d", mask_value);
        mask[buffer_offset+mask_writen] = ' ';
        delta_writen = sprintf(&delta[buffer_offset], "%d", delta_value);
        delta[buffer_offset+delta_writen] = ' ';
        buffer_offset += display_width+1;
        grid[buffer_offset] = '|';
        mask[buffer_offset] = '|';
        delta[buffer_offset] = '|';
        buffer_offset++;
        grid[buffer_offset] = ' ';
        mask[buffer_offset] = ' ';
        delta[buffer_offset] = ' ';
        buffer_offset++;
      }
      buffer_offset -= 2;
      grid[buffer_offset-1] = '\n';
      mask[buffer_offset-1] = '\n';
      delta[buffer_offset-1] = '\n';
    }
    if (w != g->size.w-1) {
      for (int z = 0; z < g->size.z; z++) {
        for (int x = 0; x < g->size.x*(display_width+1); x++) {
          grid[buffer_offset] = '-';
          mask[buffer_offset] = '-';
          delta[buffer_offset] = '-';
          buffer_offset++;
        }
        grid[buffer_offset] = '+';
        mask[buffer_offset] = '+';
        delta[buffer_offset] = '+';
        buffer_offset++;
        grid[buffer_offset] = '-';
        mask[buffer_offset] = '-';
        delta[buffer_offset] = '-';
        buffer_offset++;
      }
      buffer_offset -= 2;
      grid[buffer_offset-1] = '\n';
      mask[buffer_offset-1] = '\n';
      delta[buffer_offset-1] = '\n';
    }
  }

  grid[buffer_offset-1] = '\0';
  mask[buffer_offset-1] = '\0';
  delta[buffer_offset-1] = '\0';

  fprintf(f, "Grid:\n");
  fprintf(f, "%s\n\n", grid);
  fprintf(f, "Mask:\n");
  fprintf(f, "%s\n\n", mask);
  fprintf(f, "Delta:\n");
  fprintf(f, "%s\n", delta);

  free(grid);
  free(mask);
  free(delta);

  fclose(f);
  return filename;
}

char* null_first_occ(char* str, char c) {
  unsigned int i = 0;
  while (1) {
    if (str[i] != '\0') {
      if (str[i] == c) {
        str[i] = '\0';
        return &str[i+1];
      }
    } else {
      return &str[i];
    }
    i++;
  }
}

void remove_spaces(char* str) {
  unsigned int i = 0;
  while (1) {
    if (str[i] != ' ') {
      str = &str[i];
      return;
    }
    i++;
  }
}

grid* read_game_from_file(char* filename) {
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  FILE* f = fopen(filename, "r");
  if (f == NULL) exit(EXIT_FAILURE);

  while ((read = getline(&line, &len, fp)) != -1) {
    char* rest = null_first_occ(line, ':');
    remove_spaces(rest);
    fprintf(stderr, "%s\n", line);
    fprintf(stderr, "%s\n", rest);
  }

  fclose(fp);
  if (line) free(line);
  return NULL;
}

void place_mines(grid* g, MTRand r) {
  for (long i = 0; i < g->bombs; i++) {
    short t = 1;
    while (t) {
      xyzw_int p;
      p.x = genRand(&r)*g->size.x;
      p.y = genRand(&r)*g->size.y;
      p.z = genRand(&r)*g->size.z;
      p.w = genRand(&r)*g->size.w;
      short v = grid_at(g, p);
      if (v > BOMB) {
        grid_place_at(g, p, BOMB);
        t = 0;
        p.x -= 1;
        p.y -= 1;
        p.z -= 1;
        p.w -= 1;
        for (int w = 0; w < 3; w++) {
          if (p.w+w >= 0 && p.w+w < g->size.w) {
            for (int z = 0; z < 3; z++) {
              if (p.z+z >= 0 && p.z+z < g->size.z) {
                for (int y = 0; y < 3; y++) {
                  if (p.y+y >= 0 && p.y+y < g->size.y) {
                    for (int x = 0; x < 3; x++) {
                      if (p.x+x >= 0 && p.x+x < g->size.x) {
                        grid_inc_if_above_at(g, (xyzw_int){p.x+x, p.y+y, p.z+z, p.w+w}, BOMB);
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void place_empty(grid* g, MTRand r) {
  int empty_fields = g->len-g->bombs;
  if (empty_fields > 0) {
    for (long i = 0; i < empty_fields; i++) {
      short t = 1;
      while (t) {
        xyzw_int p;
        p.x = genRand(&r)*g->size.x;
        p.y = genRand(&r)*g->size.y;
        p.z = genRand(&r)*g->size.z;
        p.w = genRand(&r)*g->size.w;
        short v = grid_at(g, p);
        if (v == BOMB) {
          unsigned int bombs_in_area = 0;
          grid_place_at(g, p, 0);
          t = 0;
          p.x -= 1;
          p.y -= 1;
          p.z -= 1;
          p.w -= 1;
          for (int w = 0; w < 3; w++) {
            if (p.w+w >= 0 && p.w+w < g->size.w) {
              for (int z = 0; z < 3; z++) {
                if (p.z+z >= 0 && p.z+z < g->size.z) {
                  for (int y = 0; y < 3; y++) {
                    if (p.y+y >= 0 && p.y+y < g->size.y) {
                      for (int x = 0; x < 3; x++) {
                        if (p.x+x >= 0 && p.x+x < g->size.x) {
                          unsigned long pos = grid_pos(g, (xyzw_int){p.x+x, p.y+y, p.z+z, p.w+w});
                          if (g->grid[pos] == BOMB) {
                            bombs_in_area++;
                          } else {
                            g->grid[pos]--;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          grid_place_at(g, (xyzw_int){p.x+1, p.y+1, p.z+1, p.w+1}, bombs_in_area);
        }
      }
    }
  }
}

grid* create_grid(xyzw_int size, unsigned long bombs, unsigned int seed, unsigned int recursion_depth) {
  unsigned long len = get_size(size);
  grid* g = (grid*)malloc(sizeof(grid)+len*sizeof(short)*3);
  g->size = size;
  g->cursor = (xyzw_int){0, 0, 0, 0};
  g->len = len;
  g->uncovered = 0;
  g->bombs = bombs;
  g->flagged = 0;
  g->seed = seed;
  g->display_width = 2;
  g->state = RUNNING;
  g->start = time(0);
  g->paused = time(0);
  g->offset = 0;
  g->recursion_depth = recursion_depth;
  g->mask = &(g->grid[len]);
  g->delta = &(g->mask[len]);
  MTRand r = seedRand(g->seed);
  if (g->bombs < g->len/2) {
    for (long i = 0; i < g->len; i++) {
      g->grid[i] = 0;
    }
    place_mines(g, r);
  } else {
    for (long i = 0; i < g->len; i++) {
      g->grid[i] = BOMB;
    }
    place_empty(g, r);
  }
  memcpy(g->delta, g->grid, len*sizeof(short));
  for (long i = 0; i < g->len; i++) {
    g->mask[i] = COVERED;
  }
  return g;
}

xy_int get_cursor_position() {
  xy_int p = {0, 0};
  write(1, "\033[6n", 4);
  char buffer[16] = { 0 };
  char* endptr;
  int idx = 0;
  int n = 0;
  char ch;
  while (ch != 'R') {
    read(0, &ch, 1);
    buffer[idx] = ch;
    ++idx;
    if (ch == ';') {
      n = idx;
      p.x = strtol(&buffer[1], &endptr, 10);
    }
  }
  buffer[idx] = '\0';
  p.y = strtol(&endptr[1], (char**)NULL, 10);
  return p;
}

void print_at(xy_int p) {
  printf("\e[%d;%dH", p.y, p.x);
}

void move_terminal_cursor_down() {
  printf("\eE");
}

void print_str_at(xy_int p, char* s) {
  printf("\e[%d;%dH%s", p.y, p.x, s);
}

void repeat_str_at(xy_int p, char c, unsigned int amount) {
  printf("\e[%d;%dH", p.y, p.x);
  //printf("%c\e[%db", c, amount-1);
  if (amount) printf("%*c", amount, c);
}

void print_short_at(xy_int p, short n) {
  if (n < 10) {
    int str = 0xff10+n;
    printf("\e[%d;%dH%lc", p.y, p.x, str);
  } else {
    printf("\e[%d;%dH%d", p.y, p.x, n);
  }
}

void print_wide_str_with_buffer(grid* g, xy_int p, char* s, unsigned int print_buffer) {
  print_buffer--;
  repeat_str_at(p, ' ', print_buffer);
  printf("%s", s);
  //fprintf(stderr, "%d %d\n", print_buffer, g->display_width-print_buffer-2);
  if (g->display_width-print_buffer-2) printf("%*c", g->display_width-print_buffer-2, ' ');
}

void print_short_with_buffer(grid* g, xy_int p, short n, unsigned int print_buffer) {
  if (n < 0) {
    unsigned int buffer = g->display_width-(int)log10(-n)-2;
    unsigned int half_buffer = buffer/2;
    //fprintf(stderr, "%d %d %d\n", buffer, half_buffer, buffer-half_buffer);
    printf("\e[%d;%dH", p.y, p.x);
    if (half_buffer) printf("%*c", half_buffer, ' ');
    printf("%d", n);
    if (buffer-half_buffer) printf("%*c", buffer-half_buffer, ' ');
  } else if (n < 10) {
    int str = 0xff10+n;
    repeat_str_at(p, ' ', print_buffer-1);
    printf("%lc", str);
    if (g->display_width-print_buffer-1) printf("%*c", g->display_width-print_buffer-1, ' ');
  } else {
    unsigned int buffer = g->display_width-(int)log10(n)-1;
    unsigned int half_buffer = buffer/2;
    //fprintf(stderr, "%d %d %d\n", buffer, half_buffer, buffer-half_buffer);
    printf("\e[%d;%dH", p.y, p.x);
    if (half_buffer) printf("%*c", half_buffer, ' ');
    printf("%d", n);
    if (buffer-half_buffer) printf("%*c", buffer-half_buffer, ' ');
  }
}

void set_bg_colour(short* c) {
  printf("\e[48;2;%d;%d;%dm", c[0], c[1], c[2]);
}

void set_fg_colour(short* c) {
  printf("\e[38;2;%d;%d;%dm", c[0], c[1], c[2]);
}

xy_int xyzw_int_to_xy_int(grid* g, xyzw_int p) {
  xy_int p2;
  p2.x = (p.z*(g->size.x+1)+p.x)*g->display_width+1;
  p2.y = p.w*(g->size.y+1)+p.y+1;
  return p2;
}

void print_info(grid* g) {
  printf("\e[0m");
  xy_int p;
  p.x = (g->size.z*(g->size.x+1))*g->display_width+2;
  p.y = 2;
  print_str_at(p, "Game info:");
  p.y += 1;
  print_at(p); printf("Seed: %u", g->seed);
  p.y += 1;
  print_at(p); printf("Fields uncovered: %lu/%lu", g->uncovered, g->len-g->bombs);
  p.y += 1;
  print_at(p); printf("Bombs flagged: %lu/%lu", g->flagged, g->bombs);
  p.y += 1;
  print_at(p); printf("Started at: %s", ctime(&g->start));
  p.y += 1;
  p.y += 1;
  print_at(p);
  printf("\e[K");
  switch (g->state) {
    case RUNNING:
      printf("Game running");
      break;
    case PAUSED:
      printf("Game paused");
      break;
    case REVEAL_FIELD:
    case CLICKED_BOMB:
      printf("Game over");
      break;
    case GAVE_UP:
      printf("Game capitulated");
      break;
    case WIN:
      printf("Game won");
      break;
  }
}

void remove_info(grid* g) {
  printf("\e[0m");
  xy_int p;
  p.x = (g->size.z*(g->size.x+1))*g->display_width+2;
  p.y = 2;
  print_at(p);
  for (int i = 0; i < 8; i++) {
    printf("\e[K");
    p.y++;
    print_at(p);
  }
}

unsigned int* format_timer(unsigned int* formated, unsigned int seconds) {
  formated[0] = seconds/3600;
  formated[1] = (seconds%3600)/60;
  formated[2] = seconds%60;
  return formated;
}

void print_timer(timer* t) {
  print_at(t->pos);
  unsigned int formated[3];
  format_timer(formated, (time(0)-t->g->paused)+t->g->offset);
  printf("\e[0m\e[KTime elapsed: %d:%02d:%02d", formated[0], formated[1], formated[2]);
}

void* update_time_elapsed(void* args) {
  timer* t = args;
  while (1) {
    print_timer(t);
    sleep(1);
  }
}

void print_paused_time_elapsed(grid* g, xy_int pos) {
  print_at(pos);
  unsigned int formated[3];
  format_timer(formated, g->offset);
  printf("\e[0m\e[KTime elapsed: %d:%02d:%02d", formated[0], formated[1], formated[2]);
}

void remove_grid(grid* g) {
  xy_int bottom_right_corner = xyzw_int_to_xy_int(g, (xyzw_int){g->size.x-1, g->size.y-1, g->size.z-1, g->size.w-1});
  for (int i = 0; i < bottom_right_corner.y; i++) {
    printf("\e[%d;%dH\e[1K", i, bottom_right_corner.x);
  }
}

short grid_value(grid* g, unsigned long pos) {
  return g->grid[pos];
}

short delta_value(grid* g, unsigned long pos) {
  return g->delta[pos];
}

short check_around_if_covered(grid* g, xyzw_int p) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  unsigned long pos = grid_pos(g, (xyzw_int){p.x+x, p.y+y, p.z+z, p.w+w});
                  if (g->mask[pos] == COVERED) {
                    return 1;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

void print_field(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos)), short print_zero) {
  set_fg_colour(BLACK);
  unsigned int print_buffer = (g->display_width)/2;
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    unsigned long pos = grid_pos(g, p);
    xy_int p2 = xyzw_int_to_xy_int(g, p);
    if (g->mask[pos] == UNCOVERED) {
      if (g->grid[pos] == BOMB) {
        print_wide_str_with_buffer(g, p2, "💣", print_buffer);
        //repeat_str_at(p2, ' ', print_buffer-1);
        //printf("💣%*c", g->display_width-print_buffer-1, ' ');
      } else {
        if (get_value(g, pos) != 0) {
          print_short_with_buffer(g, p2, (*get_value)(g, pos), print_buffer);
        } else {
          if (print_zero) {
            if (check_around_if_covered(g, p)) {
              print_short_with_buffer(g, p2, (*get_value)(g, pos), print_buffer);
            } else {
              repeat_str_at(p2, ' ', g->display_width);
            }
          } else {
            repeat_str_at(p2, ' ', g->display_width);
          }
        }
      }
    } else if (g->mask[pos] > COVERED) {
      set_fg_colour(RED);
      print_wide_str_with_buffer(g, p2, "🏴", print_buffer);
      //repeat_str_at(p2, ' ', print_buffer-1);
      //printf("🏴%*c", g->display_width-print_buffer-1, ' ');
    } else {
      repeat_str_at(p2, ' ', g->display_width);
    }
  }
}

void set_checkered_bg(grid* g, xyzw_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    unsigned long pos = grid_pos(g, p);
    if ((p.x+p.y%2)%2) {
      if (g->mask[pos] > UNCOVERED) {
        set_bg_colour(COVERED_DARK);
      } else {
        set_bg_colour(UNCOVERED_DARK);
      }
    } else {
      if (g->mask[pos] > UNCOVERED) {
        set_bg_colour(COVERED_LIGHT);
      } else {
        set_bg_colour(UNCOVERED_LIGHT);
      }
    }
  }
}

void print_grid(grid* g, short ((*get_value)(grid* g, unsigned long pos))) {
  set_fg_colour(BLACK);
  printf("\e[0m");
  remove_grid(g);
  for (int w = 0; w < g->size.w; w++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzw_int p = {x, y, z, w};
          set_checkered_bg(g, p);
          print_field(g, p, (*get_value), 1);
        }
      }
    }
  }
}

void print_field_paused(grid* g, xyzw_int p) {
  repeat_str_at(xyzw_int_to_xy_int(g, p), ' ', g->display_width);
}

void set_checkered_bg_paused(grid* g, xyzw_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    if ((p.x+p.y%2)%2) {
      set_bg_colour(PAUSED_DARK);
    } else {
      set_bg_colour(PAUAED_LIGHT);
    }
  }
}

void print_grid_paused(grid* g) {
  set_fg_colour(BLACK);
  printf("\e[0m");
  remove_grid(g);
  for (int w = 0; w < g->size.w; w++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzw_int p = {x, y, z, w};
          set_checkered_bg_paused(g, p);
          print_field_paused(g, p);
        }
      }
    }
  }
}

void print_field_uncovered(grid* g, xyzw_int p, unsigned long pos, short ((*get_value)(grid* g, unsigned long pos))) {
  set_fg_colour(BLACK);
  unsigned int print_buffer = (g->display_width)/2;
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    xy_int p2 = xyzw_int_to_xy_int(g, p);
    if (g->mask[pos] > COVERED) {
      if (g->grid[pos] != BOMB) {
        set_bg_colour(RED);
      }
      print_wide_str_with_buffer(g, p2, "🏴", print_buffer);
    } else {
      if (g->grid[pos] == BOMB) {
        print_wide_str_with_buffer(g, p2, "💣", print_buffer);
      } else {
        if (get_value(g, pos) != 0) {
          print_short_with_buffer(g, p2, (*get_value)(g, pos), print_buffer);
        } else {
          repeat_str_at(p2, ' ', g->display_width);
        }
      }
    }
  }
}

void print_grid_uncovered(grid* g, short ((*get_value)(grid* g, unsigned long pos))) {
  set_fg_colour(BLACK);
  printf("\e[0m");
  remove_grid(g);
  for (int w = 0; w < g->size.w; w++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzw_int p = {x, y, z, w};
          unsigned long pos = grid_pos(g, p);
          if (g->mask[pos] == COVERED) {
            g->mask[pos] = UNCOVERED;
            g->uncovered += 1;
          }
          set_checkered_bg(g, p);
          print_field_uncovered(g, p, pos, (*get_value));
        }
      }
    }
  }
}

void uncover_field(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos)));

void uncover_field(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  if (g->recursion_depth) {
    unsigned long pos = grid_pos(g, p);
    if (g->mask[pos] == COVERED) {
      g->mask[pos] = UNCOVERED;
      g->uncovered += 1;
      set_checkered_bg(g, p);
      print_field(g, p, (*get_value), 0);
      if (g->grid[pos] == BOMB) {
        g->state = CLICKED_BOMB;
      } else if (g->grid[pos] == 0) {
        unsigned int recursion_depth = g->recursion_depth;
        g->recursion_depth -= 1;
        p.x -= 1;
        p.y -= 1;
        p.z -= 1;
        p.w -= 1;
        for (int w = 0; w < 3; w++) {
          if (p.w+w >= 0 && p.w+w < g->size.w) {
            for (int z = 0; z < 3; z++) {
              if (p.z+z >= 0 && p.z+z < g->size.z) {
                for (int y = 0; y < 3; y++) {
                  if (p.y+y >= 0 && p.y+y < g->size.y) {
                    for (int x = 0; x < 3; x++) {
                      if (p.x+x >= 0 && p.x+x < g->size.x) {
                        xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                        pos = grid_pos(g, p2);
                        if (g->mask[pos] == COVERED) {
                          uncover_field(g, p2, (*get_value));
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        g->recursion_depth = recursion_depth;
      }
    } else {
      if (g->delta[pos] == 0) {
        p.x -= 1;
        p.y -= 1;
        p.z -= 1;
        p.w -= 1;
        for (int w = 0; w < 3; w++) {
          if (p.w+w >= 0 && p.w+w < g->size.w) {
            for (int z = 0; z < 3; z++) {
              if (p.z+z >= 0 && p.z+z < g->size.z) {
                for (int y = 0; y < 3; y++) {
                  if (p.y+y >= 0 && p.y+y < g->size.y) {
                    for (int x = 0; x < 3; x++) {
                      if (p.x+x >= 0 && p.x+x < g->size.x) {
                        xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                        pos = grid_pos(g, p2);
                        if (g->mask[pos] == COVERED) {
                          g->mask[pos] = UNCOVERED;
                          g->uncovered += 1;
                          if (g->grid[pos] == BOMB) g->state = CLICKED_BOMB;
                          uncover_field(g, p2, (*get_value));
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        print_grid(g, get_value);
      }
    }
    if (g->uncovered == g->len-g->bombs && g->state != CLICKED_BOMB) {
      g->state = WIN;
    }
  }
}

short find_empty(grid* g, short ((*get_value)(grid* g, unsigned long pos))) {
  for (int w = 0; w < g->size.w; w++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzw_int p = {x, y, z, w};
          if (grid_at(g, p) == 0) {
            uncover_field(g, p, get_value);
            return 0;
          }
        }
      }
    }
  }
  return 1;
}

void dec_delta(grid* g, unsigned long pos) {
  g->delta[pos]--;
}

void inc_delta(grid* g, unsigned long pos) {
  g->delta[pos]++;
}

void mark_field(grid* g, xyzw_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.w < g->size.w) {
    void (*change_delta)(grid* g, unsigned long pos) = NULL;
    unsigned long pos = grid_pos(g, p);
    if (g->mask[pos] > COVERED) {
      g->mask[pos] -= 2; //unflag
      g->flagged -= 1;
      change_delta = inc_delta;
    } else {
      g->mask[pos] += 2; //flag
      g->flagged += 1;
      change_delta = dec_delta;
    }
    p.x -= 1;
    p.y -= 1;
    p.z -= 1;
    p.w -= 1;
    for (int w = 0; w < 3; w++) {
      if (p.w+w >= 0 && p.w+w < g->size.w) {
        for (int z = 0; z < 3; z++) {
          if (p.z+z >= 0 && p.z+z < g->size.z) {
            for (int y = 0; y < 3; y++) {
              if (p.y+y >= 0 && p.y+y < g->size.y) {
                for (int x = 0; x < 3; x++) {
                  if (p.x+x >= 0 && p.x+x < g->size.x) {
                    xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                    pos = grid_pos(g, p2);
                    (*change_delta)(g, pos);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void mark_field_chording(grid* g, xyzw_int p) {
  unsigned long pos = grid_pos(g, p);
  if (g->mask[pos] >= COVERED) {
    void (*change_delta)(grid* g, unsigned long pos) = NULL;
    if (g->mask[pos] > COVERED) {
      g->mask[pos] -= 2; //unflag
      g->flagged -= 1;
      change_delta = inc_delta;
    } else {
      g->mask[pos] += 2; //flag
      g->flagged += 1;
      change_delta = dec_delta;
    }
    p.x -= 1;
    p.y -= 1;
    p.z -= 1;
    p.w -= 1;
    for (int w = 0; w < 3; w++) {
      if (p.w+w >= 0 && p.w+w < g->size.w) {
        for (int z = 0; z < 3; z++) {
          if (p.z+z >= 0 && p.z+z < g->size.z) {
            for (int y = 0; y < 3; y++) {
              if (p.y+y >= 0 && p.y+y < g->size.y) {
                for (int x = 0; x < 3; x++) {
                  if (p.x+x >= 0 && p.x+x < g->size.x) {
                    xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                    pos = grid_pos(g, p2);
                    (*change_delta)(g, pos);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void set_checkered_bg_cursor(grid* g, xyzw_int p, unsigned long pos) {
  if ((p.x+p.y%2)%2) {
    if (g->mask[pos] > UNCOVERED) {
      set_bg_colour(COVERED_PINK_DARK);
    } else {
      set_bg_colour(UNCOVERED_PINK_DARK);
    }
  } else {
    if (g->mask[pos] > UNCOVERED) {
      set_bg_colour(COVERED_PINK_LIGHT);
    } else {
      set_bg_colour(UNCOVERED_PINK_LIGHT);
    }
  }
}

void print_cursor(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  set_bg_colour(PINK);
  print_field(g, p, (*get_value), 1);
}

void print_area_of_influence(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  unsigned long pos = grid_pos(g, p2);
                  set_checkered_bg_cursor(g, p2, pos);
                  print_field(g, p2, (*get_value), 1);
                }
              }
            }
          }
        }
      }
    }
  }
}

void print_cursor_all(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  print_area_of_influence(g, p, get_value);
  print_cursor(g, p, get_value);
}

/*void remove_cursor(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  set_checkered_bg(g, p);
  print_field(g, p, (*get_value), 1);
}*/

void remove_area_of_influence(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  set_checkered_bg(g, p2);
                  print_field(g, p2, (*get_value), 1);
                }
              }
            }
          }
        }
      }
    }
  }
}

void print_cursor_paused(grid* g, xyzw_int p) {
  set_bg_colour(PINK);
  print_field_paused(g, p);
}

void print_area_of_influence_paused(grid* g, xyzw_int p) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  unsigned long pos = grid_pos(g, p2);
                  if ((p2.x+p2.y%2)%2) {
                    set_bg_colour(PAUSED_PINK_DARK);
                  } else {
                    set_bg_colour(PAUSED_PINK_LIGHT);
                  }
                  print_field_paused(g, p2);
                }
              }
            }
          }
        }
      }
    }
  }
}

void print_cursor_paused_all(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  print_area_of_influence_paused(g, p);
  print_cursor_paused(g, p);
}

/*void remove_cursor_paused(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  set_checkered_bg_paused(g, p);
  print_field_paused(g, p);
}*/

void remove_area_of_influence_paused(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  set_checkered_bg_paused(g, p2);
                  print_field_paused(g, p2);
                }
              }
            }
          }
        }
      }
    }
  }
}

void print_area_of_influence_uncovered(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  unsigned long pos = grid_pos(g, p2);
                  set_checkered_bg_cursor(g, p2, pos);
                  print_field_uncovered(g, p2, pos, get_value);
                }
              }
            }
          }
        }
      }
    }
  }
}

void print_cursor_uncovered_all(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  print_area_of_influence_uncovered(g, p, get_value);
  print_cursor(g, p, get_value);
}

void remove_area_of_influence_uncovered(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos))) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.w -= 1;
  for (int w = 0; w < 3; w++) {
    if (p.w+w >= 0 && p.w+w < g->size.w) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzw_int p2 = {p.x+x, p.y+y, p.z+z, p.w+w};
                  set_checkered_bg(g, p2);
                  print_field_uncovered(g, p2, grid_pos(g, p2), get_value);
                }
              }
            }
          }
        }
      }
    }
  }
}


void inc_option(option* op, int i, int dif) {
  switch (op[i].type) {
    case BOOL:
      op[i].value = (op[i].value+1)%2;
      break;
    case UINT:
      if (op[i].value+dif >= 0) {
        op[i].value += dif;
      }
      break;
    case EMPTY:
    case DONE:
      break;
  }
}

void remove_option(option* op, int i) {
  /*int o = option_offset;
  switch (op[i].type) {
    case BOOL:
      o += (op[i].value)?4:5;
      break;
    case UINT:
      o += log10(op[i].value)+1;
      break;
    case EMPTY:
    case DONE:
      break;
  }*/
  printf("\e[%dC", option_offset+numbuffer_len);
  printf("\e[1K");
}

int print_option(option* op, int i) {
  printf("\e[%dC", op[i].depth);
  switch (op[i].type) {
    case BOOL:
      printf("%s:\e[%dG%s", op[i].name, option_offset, (op[i].value)?"True":"False");
      break;
    case UINT:
      printf("%s:\e[%dG%d", op[i].name, option_offset, op[i].value);
      break;
    case EMPTY:
      printf("%s", op[i].name);
      break;
    case DONE:
      return STOP;
  }
  return CONTINUE;
}

void print_option_cursor(option* op, int i, xy_int p) {
  print_at((xy_int){p.x, p.y+i});
  remove_option(op, i);
  print_at((xy_int){p.x, p.y+i});
  printf("\e[4m");
  print_option(op, i);
  printf("\e[0m");
}

void print_settings_controls(xy_int p) {
  print_at(p);
  printf("Controls for the settings:");
  printf("\e[B\e[%dG", p.x); printf("  \e[3mAny up movement key\e[0m:      move selection up");
  printf("\e[B\e[%dG", p.x); printf("  \e[3mAny down movement key\e[0m:    move selection down");
  printf("\e[B\e[%dG", p.x); printf("  \e[3mAny left movement key\e[0m:    decrement selection");
  printf("\e[B\e[%dG", p.x); printf("  \e[3mAny right movement key\e[0m:   increment selection");
  printf("\e[B\e[%dG", p.x); printf("  o:                        apply and exit settings");
  printf("\e[B\e[%dG", p.x); printf("  q:                        discard and exit settings");
}

grid* print_settings(option* op, grid* g) {
  char numbuffer[numbuffer_len];
  numbuffer[0] = '\0';
  unsigned int numbufferpos = 0;
  set_fg_colour(BLACK);
  printf("\e[0m");
  printf("\e[2J");
  xy_int p = {2, 2};
  int i = 0;
  short t = 1;
  while(t) {
    print_at((xy_int){p.x, p.y+i});
    if (print_option(op, i)) {
      t = 0;
    }
    i++;
  }
  size_t opsize = sizeof(option)*i;
  option* backup = malloc(opsize);
  memcpy(backup, op, opsize);
  i--;
  int cursor = 0;
  print_settings_controls((xy_int){option_offset+20, 2});
  print_option_cursor(op, cursor, p);
  while (1) {
    int c = getchar();
    switch (c) {
      case 100: //d
      case 68: //right arrow
      case 104: //h
      case 8: //ctrl+h
        inc_option(op, cursor, -1);
        break;
      case 97: //a
      case 67: //left arrow
      case 108: //l
      case 12: //ctrl+l
        inc_option(op, cursor, 1);
        break;
      case 115: //s
      case 66: //down arrow
      case 106: //j
      case 10: //ctrl+j
        print_at((xy_int){p.x, p.y+cursor});
        print_option(op, cursor);
        if (cursor+1 < i) {
          cursor += 1;
        } else {
          cursor = 0;
        }
        numbufferpos = 0;
        numbuffer[numbufferpos] = '\0';
        break;
      case 119: //w
      case 65: //up arrow
      case 107: //k
      case 11: //ctrl+k
        print_at((xy_int){p.x, p.y+cursor});
        print_option(op, cursor);
        if (cursor-1 >= 0) {
          cursor -= 1;
        } else {
          cursor = i-1;
        }
        numbufferpos = 0;
        numbuffer[numbufferpos] = '\0';
        break;
      case 48: //numbers 0-9
      case 49:
      case 50:
      case 51:
      case 52:
      case 53:
      case 54:
      case 55:
      case 56:
      case 57:
        if (numbufferpos < numbuffer_len-1 && op[cursor].type == UINT) {
          numbuffer[numbufferpos] = c;
          numbufferpos++;
          numbuffer[numbufferpos] = '\0';
          op[cursor].value = strtol(numbuffer, (char**)NULL, 10);
        }
        break;
      case 127: //del
        if (numbufferpos > 0) {
          numbufferpos--;
          numbuffer[numbufferpos] = '\0';
          op[cursor].value = strtol(numbuffer, (char**)NULL, 10);
        }
        if (numbufferpos == 0) {
          op[cursor].value = 1;
        }
        break;
      case 111: //o
        free(g);
        xyzw_int size = {op[1].value, op[2].value, op[3].value, op[4].value};
        //print_xyzw(size);
        g = create_grid(size, op[7].value, (op[5].value)?time(0):op[6].value, op[8].value);
        free(backup);
        return g;
      case 113: //q
        memcpy(op, backup, opsize);
        free(backup);
        return g;
      default:
        numbufferpos = 0;
        numbuffer[numbufferpos] = '\0';
    }
    print_option_cursor(op, cursor, p);
  }
}

void print_controls() {
  printf("Controls:");
  move_terminal_cursor_down(); printf("  Move right in x:       \e[3mright arrow\e[0m, l");
  move_terminal_cursor_down(); printf("  Move left in x:        \e[3mleft arrow\e[0m, h");
  move_terminal_cursor_down(); printf("  Move up in y:          \e[3mup arrow\e[0m, k");
  move_terminal_cursor_down(); printf("  Move down in y:        \e[3mdown arrow\e[0m, j");
  move_terminal_cursor_down(); printf("  Move right in z:       d, ctrl-l");
  move_terminal_cursor_down(); printf("  Move left in z:        a, ctrl-h");
  move_terminal_cursor_down(); printf("  Move up in q:          w, ctrl-k");
  move_terminal_cursor_down(); printf("  Move down in y:        s, ctrl-j");
  move_terminal_cursor_down(); printf("  Mark bomb:             m, e");
  move_terminal_cursor_down(); printf("  Mark bomb chording:    M, E");
  move_terminal_cursor_down(); printf("  Uncover field:         \e[3mspace\e[0m");
  move_terminal_cursor_down(); printf("  Find empty field:      f (only possible at the start of the game)");
  move_terminal_cursor_down(); printf("  Turn on delta mode:    u");
  move_terminal_cursor_down(); printf("  Give up/reveal field:  g");
  move_terminal_cursor_down(); printf("  Pause game:            p");
  move_terminal_cursor_down(); printf("  Open options:          o");
  move_terminal_cursor_down(); printf("  Start new game:        n");
  move_terminal_cursor_down(); printf("  Print controls:        c");
  move_terminal_cursor_down(); printf("  Toggle info:           i");
  move_terminal_cursor_down(); printf("  Quit game:             q");
}

void print_help_menu() {
  move_terminal_cursor_down();
  move_terminal_cursor_down(); printf("-h, -?, --help         Show this menu");
  move_terminal_cursor_down(); printf("-d, --do_random        If true, sets the seed to the current time");
  move_terminal_cursor_down(); printf("-s, --seed             Input seed as unsigned integer");
  move_terminal_cursor_down(); printf("-b, --bombs            Input amount of bombs as unsigned integer");
  move_terminal_cursor_down(); printf("-r, --recursion_depth  The amount of recursion allowed when uncovering fields");
  move_terminal_cursor_down(); printf("-a, --area, --size     Size of the game (must be given as a comma separated list of unsigned integers e.g.: 4, 4, 4, 4)");
  move_terminal_cursor_down(); printf("-i, --show_info        Show info about the current game. Can be set to true or false");
  move_terminal_cursor_down(); printf("-u, --show_delta       Field numbers only show unmarked bombs instead of total. Can be set to true or false");
  move_terminal_cursor_down(); printf("-g, --debug            Run in debug mode. Allows editing of field contents");
  move_terminal_cursor_down(); printf("-l, --load             Load game from save file");
}

void exit_game_failure() {
  printf("\e[0m");
  printf("\e[?47l");
  printf("\e[?25h");
  exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
  unsigned int seed = time(0);
  short do_random = 1;
  xyzw_int size = {4, 4, 4, 4};
  unsigned int bombs = 20;
  unsigned int recursion_depth = 1000;
  short show_info = 1;
  short show_delta = 1;
  short debug_mode = 0;
  char numbuffer[numbuffer_len];
  numbuffer[0] = '\0';
  unsigned int numbufferpos = 0;
  short game_running = 1;
  printf("\e[?47h"); //save screen
  printf("\e[?25l"); //make cursor invisible
  printf("\e[s"); //save curosr position
  set_input_mode();
  setlocale(LC_CTYPE, "");
  printf("\e[2J");
  if (argc > 1) {
    char* endptr;
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-?") == 0 || strcmp(argv[i], "--help") == 0) {
        print_controls();
        print_help_menu();
        printf("\e[0m");
        printf("\e[?25h");
        exit(EXIT_SUCCESS);
      } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--do_random") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide a boolean value e.g.: false", argv[i-1]);
          exit_game_failure();
        }
        for (int n = 0; n < strlen(argv[i]); n++) {
          argv[i][n] = tolower(argv[i][n]);
        }
        if (strcmp(argv[i], "y") == 0 || strcmp(argv[i], "t") == 0 || strcmp(argv[i], "yes") == 0 || strcmp(argv[i], "true") == 0) {
          do_random = 1;
        } else if (strcmp(argv[i], "n") == 0 || strcmp(argv[i], "f") == 0 || strcmp(argv[i], "no") == 0 || strcmp(argv[i], "false") == 0) {
          do_random = 0;
        } else {
          printf("Ivalid option for %s %s. Valid otions are: t, f, true, false, y, n, yes, no\n", argv[i-1], argv[i]);
          printf("Capitalisation is irrelevant");
          exit_game_failure();
        }
      } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seed") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide an unsigned integer value e.g.: 10", argv[i-1]);
          exit_game_failure();
        }
        seed = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s.\n", argv[i-1], argv[i]);
          exit_game_failure();
        } else if (seed < 0) {
          printf("Negative seed value of %d was provided!\nPlease provide a positive integer", seed);
        }
      } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bombs") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide an unsigned integer e.g.: 20", argv[i-1]);
          exit_game_failure();
        }
        bombs = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s.\n", argv[i-1], argv[i]);
          exit_game_failure();
        } else if (bombs < 0) {
          printf("Negative bomb amount of %d was provided!\nPlease provide a positive integer", bombs);
        }
      } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursion_depth") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide a boolean value e.g.: false", argv[i-1]);
          exit_game_failure();
        }
        recursion_depth = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s.\n", argv[i-1], argv[i]);
          exit_game_failure();
        }
      } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--area") == 0 || strcmp(argv[i], "--size") == 0) {
        char* og = argv[i];
        i++;
        if (argc-i < 4) {
          printf("not enough integers specified! You must write 4 integers, you wrote %d\n", argc-i);
          exit_game_failure();
        }
        size.x = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s for size x.\n", og, argv[i]);
          exit_game_failure();
        } else if (size.x < 0) {
          printf("Negative size x of %d was provided!\nPlease provide a positive integer", size.x);
          exit_game_failure();
        }
        i++;
        size.y = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s for size y.\n", og, argv[i]);
          exit_game_failure();
        } else if (size.y < 0) {
          printf("Negative size y of %d was provided!\nPlease provide a positive integer", size.y);
          exit_game_failure();
        }
        i++;
        size.z = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s for size z.\n", og, argv[i]);
          exit_game_failure();
        } else if (size.z < 0) {
          printf("Negative size z of %d was provided!\nPlease provide a positive integer", size.z);
          exit_game_failure();
        }
        i++;
        size.w = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s for size w.\n", og, argv[i]);
          exit_game_failure();
        } else if (size.w < 0) {
          printf("Negative size w of %d was provided!\nPlease provide a positive integer", size.w);
          exit_game_failure();
        }
      } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--show_info") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide a boolean value e.g.: false", argv[i-1]);
          exit_game_failure();
        }
        for (int n = 0; n < strlen(argv[i]); n++) {
          argv[i][n] = tolower(argv[i][n]);
        }
        if (strcmp(argv[i], "y") == 0 || strcmp(argv[i], "t") == 0 || strcmp(argv[i], "yes") == 0 || strcmp(argv[i], "true") == 0) {
          show_info = 1;
        } else if (strcmp(argv[i], "n") == 0 || strcmp(argv[i], "f") == 0 || strcmp(argv[i], "no") == 0 || strcmp(argv[i], "false") == 0) {
          show_info = 0;
        } else {
          printf("Ivalid option for %s %s. Valid otions are: t, f, true, false, y, n, yes, no\n", argv[i-1], argv[i]);
          printf("Capitalisation is irrelevant");
          exit_game_failure();
        }
      } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--show_delta") == 0) {
        i++;
        if (i == argc) {
          printf("No value was provided for option %s!\nPlease provide a boolean value e.g.: false", argv[i-1]);
          exit_game_failure();
        }
        for (int n = 0; n < strlen(argv[i]); n++) {
          argv[i][n] = tolower(argv[i][n]);
        }
        if (strcmp(argv[i], "y") == 0 || strcmp(argv[i], "t") == 0 || strcmp(argv[i], "yes") == 0 || strcmp(argv[i], "true") == 0) {
          show_delta = 1;
        } else if (strcmp(argv[i], "n") == 0 || strcmp(argv[i], "f") == 0 || strcmp(argv[i], "no") == 0 || strcmp(argv[i], "false") == 0) {
          show_delta = 0;
        } else {
          printf("Ivalid option for %s %s. Valid otions are: t, f, true, false, y, n, yes, no\n", argv[i-1], argv[i]);
          printf("Capitalisation is irrelevant");
          exit_game_failure();
        }
      } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
        debug_mode = 1;
      }
    }
  }
  option op[12] = {
    {1, 0, 0, EMPTY, "Size"},
    {1, size.x, 2, UINT, "x"},
    {1, size.y, 2, UINT, "y"},
    {1, size.z, 2, UINT, "z"},
    {1, size.w, 2, UINT, "w"},
    {1, do_random, 0, BOOL, "Do random"},
    {1, seed, 2, UINT, "Seed"},
    {1, bombs, 0, UINT, "Bombs"},
    {1, recursion_depth, 0, UINT, "Recusion depth"},
    {1, show_info, 0, BOOL, "Show info"},
    {1, show_delta, 0, BOOL, "Show deltas"},
    {0, 0, 0, DONE, ""},
  };

  printf("\e]0;4d minesweeper\a");
  short (*get_value)(grid* g, unsigned long pos) = NULL;
  void (*print_cursor_func)(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos)));
  void (*remove_cursor_func)(grid* g, xyzw_int p, short ((*get_value)(grid* g, unsigned long pos)));
  if (op[10].value) {
    get_value = delta_value;
  } else {
    get_value = grid_value;
  }
  grid* g = create_grid(size, op[7].value, (op[5].value)?time(0):op[6].value, op[8].value);
  if (g->state == PAUSED) {
    print_cursor_func = print_cursor_paused_all;
    remove_cursor_func = remove_area_of_influence_paused;
  } else {
    print_cursor_func = print_cursor_all;
    remove_cursor_func = remove_area_of_influence;
  }
  unsigned int display_width = g->display_width;
  print_grid(g, get_value);
  xyzw_int cursor = g->cursor;
  (*print_cursor_func)(g, cursor, get_value);
  timer t = {(xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7}, g};
  pthread_t timerthread;
  if (op[9].value) {
    print_info(g);
    print_timer(&t);
  }
  
  char* saved_file = save_game_to_file(g);
  if (strcmp(saved_file, "NO_FILE")) read_game_from_file(saved_file);

  int c = getchar();
  g->start = time(0);
  g->paused = g->start;
  if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
  while (1) {
    remove_cursor_func(g, cursor, get_value);
    //remove_cursor(g, cursor, get_value);
    if (debug_mode) {
      fprintf(stderr, "%d\n", c);
      switch (c) {
        case 48: //numbers 0-9
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
        case 56:
        case 57:
          if (numbufferpos < numbuffer_len-1) {
            numbuffer[numbufferpos] = c;
            numbufferpos++;
            numbuffer[numbufferpos] = '\0';
            grid_place_at(g, cursor, strtol(numbuffer, (char**)NULL, 10));
          }
          break;
        default:
          numbufferpos = 0;
          numbuffer[numbufferpos] = '\0';
      }
      if (display_width != g->display_width) {
        xy_int bn = find_biggest_number(g);
        display_width = (op[10].value)?(bn.x > bn.y)?bn.x:bn.y:bn.x;
        g->display_width = display_width;
        printf("\e[2J");
        print_grid(g, get_value);
        t.pos = (xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7};
        if (op[9].value) print_info(g);
        fprintf(stderr, "%d\n", g->display_width);
      }
    }
    short continue_after = 0;
    switch (c) {
      case 68: //right arrow
      case 104: //h
        if (cursor.x-1 >= 0) {
          cursor.x -= 1;
        }
        c = 0;
        break;
      case 67: //left arrow
      case 108: //l
        if (cursor.x+1 < g->size.x) {
          cursor.x += 1;
        }
        c = 0;
        break;
      case 66: //down arrow
      case 106: //j
        if (cursor.y+1 < g->size.y) {
          cursor.y += 1;
        }
        c = 0;
        break;
      case 65: //up arrow
      case 107: //k
        if (cursor.y-1 >= 0) {
          cursor.y -= 1;
        }
        c = 0;
        break;
      case 97: //a
      case 8: //ctrl+h
        if (cursor.z-1 >= 0) {
          cursor.z -= 1;
        }
        c = 0;
        break;
      case 100: //d
      case 12: //ctrl+l
        if (cursor.z+1 < g->size.z) {
          cursor.z += 1;
        }
        c = 0;
        break;
      case 115: //s
      case 10: //ctrl+j
        if (cursor.w+1 < g->size.w) {
          cursor.w += 1;
        }
        c = 0;
        break;
      case 119: //w
      case 11: //ctrl+k
        if (cursor.w-1 >= 0) {
          cursor.w -= 1;
        }
        c = 0;
        break;
      case 32: //*space*
        if (g->state != PAUSED) {
          uncover_field(g, cursor, get_value);
          if (g->state > PAUSED && game_running) {
            game_running = 0;
            g->offset += time(0)-g->paused;
            if (op[9].value) {
              pthread_cancel(timerthread);
              print_paused_time_elapsed(g, t.pos);
              print_info(g);
            }
          } else {
            if (op[9].value) print_info(g);
          }
        }
        c = 0;
        break;
      case 101: //e
      case 109: //m
        if (g->state != PAUSED) {
          mark_field(g, cursor);
          xy_int bn = find_biggest_number(g);
          display_width = (op[10].value)?(bn.x > bn.y)?bn.x:bn.y:bn.x;
          if (g->display_width != display_width) {
            g->display_width = display_width;
            printf("\e[0m");
            printf("\e[2J");
            t.pos = (xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7};
            print_timer(&t);
            if (g->state == GAVE_UP || g->state == REVEAL_FIELD) {
              print_grid_uncovered(g, get_value);
            } else {
              print_grid(g, get_value);
            }
          }
          if (op[9].value) print_info(g);
        }
        c = 0;
        break;
      case 69: //E
      case 77: //M
        if (g->state != PAUSED) {
          cursor.x--;
          cursor.y--;
          cursor.z--;
          cursor.w--;
          for (int w = 0; w < 3; w++) {
            if (cursor.w+w >= 0 && cursor.w+w < g->size.w) {
              for (int z = 0; z < 3; z++) {
                if (cursor.z+z >= 0 && cursor.z+z < g->size.z) {
                  for (int y = 0; y < 3; y++) {
                    if (cursor.y+y >= 0 && cursor.y+y < g->size.y) {
                      for (int x = 0; x < 3; x++) {
                        if (cursor.x+x >= 0 && cursor.x+x < g->size.x) {
                          xyzw_int p2 = {cursor.x+x, cursor.y+y, cursor.z+z, cursor.w+w};
                          mark_field_chording(g, p2);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          cursor.x++;
          cursor.y++;
          cursor.z++;
          cursor.w++;
          xy_int bn = find_biggest_number(g);
          display_width = (op[10].value)?(bn.x > bn.y)?bn.x:bn.y:bn.x;
          if (g->display_width != display_width) {
            g->display_width = display_width;
            printf("\e[0m");
            printf("\e[2J");
            t.pos = (xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7};
            print_timer(&t);
            if (g->state == GAVE_UP || g->state == REVEAL_FIELD) {
              print_grid_uncovered(g, get_value);
            } else {
              print_grid(g, get_value);
            }
          }
          if (op[9].value) print_info(g);
        }
        c = 0;
        break;
      case 117: //u
        if (op[10].value) {
          op[10].value = 0;
          get_value = grid_value;
        } else {
          op[10].value = 1;
          get_value = delta_value;
        }
        xy_int bn = find_biggest_number(g);
        display_width = (op[10].value)?(bn.x > bn.y)?bn.x:bn.y:bn.x;
        if (g->display_width != display_width) {
          g->display_width = display_width;
          printf("\e[0m");
          printf("\e[2J");
          t.pos = (xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7};
        }
        print_grid(g, get_value);
        if (op[9].value) print_info(g);
        c = 0;
        break;
      case 103: //g
        if (g->state != PAUSED && g->state < GAVE_UP) {
          game_running = 0;
          if (g->state != CLICKED_BOMB) {
            g->state = GAVE_UP;
            g->offset += time(0)-g->paused;
          } else {
            g->state = REVEAL_FIELD;
          }
          print_cursor_func = print_cursor_uncovered_all;
          remove_cursor_func = remove_area_of_influence_uncovered;
          print_grid_uncovered(g, get_value);
          if (op[9].value) {
            pthread_cancel(timerthread);
            print_paused_time_elapsed(g, t.pos);
            print_info(g);
          }
        }
        c = 0;
        break;
      case 112: {//p
        if (g->state == RUNNING) {
          g->state = PAUSED;
          g->offset += time(0)-g->paused;
          if (op[9].value) {
            pthread_cancel(timerthread);
            print_paused_time_elapsed(g, t.pos);
          }
          print_cursor_func = print_cursor_paused_all;
          remove_cursor_func = remove_area_of_influence_paused;
          print_grid_paused(g);
        } else if (g->state == PAUSED) {
          g->state = RUNNING;
          g->paused = time(0);
          if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
          print_cursor_func = print_cursor_all;
          remove_cursor_func = remove_area_of_influence;
          print_grid(g, get_value);
        }
        if (op[9].value) print_info(g);
        c = 0;
        break;
      } case 111: //o
        if (g->state == RUNNING) {
          continue_after = 1;
          g->state = PAUSED;
          g->offset += time(0)-g->paused;
          if (op[9].value) pthread_cancel(timerthread);
        }
        g = print_settings(op, g);
        printf("\e[0m\e[2J");
        t = (timer){(xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7}, g};
        cursor = g->cursor;
        if (continue_after) {
          g->state = RUNNING;
          g->paused = time(0);
          if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
          print_grid(g, get_value);
        } else {
          g->state = PAUSED;
          print_grid_paused(g);
        }
        if (op[9].value) {
          print_paused_time_elapsed(g, t.pos);
          print_info(g);
        }
        c = 0;
        break;
      case 110: //n
        game_running = 1;
        int game_state = g->state;
        free(g);
        size = (xyzw_int){op[1].value, op[2].value, op[3].value, op[4].value};
        g = create_grid(size, op[7].value, (op[5].value)?time(0):op[6].value, op[8].value);
        print_cursor_func = print_cursor_all;
        remove_cursor_func = remove_area_of_influence;
        printf("\e[0m");
        printf("\e[2J");
        print_grid(g, get_value);
        g->offset = 0;
        g->paused = time(0);
        t = (timer){(xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7}, g};
        cursor = g->cursor;
        if (op[9].value) {
          print_info(g);
          print_timer(&t);
        }
        (*print_cursor_func)(g, cursor, get_value);
        c = getchar();
        g->start = time(0);
        g->paused = g->start;
        if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        break;
      case 99: //c
        continue_after = 0;
        if (g->state == RUNNING) {
          g->state = PAUSED;
          g->offset = time(0)-g->paused;
          if (op[9].value) pthread_cancel(timerthread);
        }
        printf("\e[0m");
        printf("\e[2J");
        print_at((xy_int){2, 2});
        print_controls();
        getchar();
        if (continue_after) {
          g->state = RUNNING;
          g->paused = time(0);
          print_grid(g, get_value);
          if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        } else {
          print_grid_paused(g);
        }
        if (op[9].value) print_info(g);
        c = 0;
        break;
      case 102: //f
        if (g->uncovered == 0 && g->state == RUNNING) {
          if (find_empty(g, get_value)) {
            print_str_at((xy_int){t.pos.x, 9}, "\e[0mDidn't find any empty fields");
          } else {
            if (op[9].value) print_info(g);
          }
        }
        c = 0;
        break;
      case 105: //i
        if (op[9].value) {
          op[9].value = 0;
          pthread_cancel(timerthread);
          remove_info(g);
        } else {
          op[9].value = 1;
          print_info(g);
          pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        }
        c = 0;
        break;
      case 113: //q
        exit_game();
        free(g);
        exit(EXIT_SUCCESS);
      default:
        (*print_cursor_func)(g, cursor, get_value);
        c = getchar();
    }
  }
}
