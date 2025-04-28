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

typedef struct _xyzq_int {
  int x, y, z, q;
} xyzq_int;

typedef struct _xy_int {
  int x, y;
} xy_int;

typedef struct _grid {
  xyzq_int size;
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
  unsigned int recusion_depth;
  short* mask;
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
  WIN,
};

short COVERED_LIGHT[3] = {0x66, 0x66, 0x66};
short COVERED_DARK[3] = {0x3b, 0x3b, 0x3b};
short UNCOVERED_LIGHT[3] = {0xc6, 0xc6, 0xc6};
short UNCOVERED_DARK[3] = {0xb8, 0xb8, 0xb8};
short BLACK[3] = {0, 0, 0};
short PINK[3] = {255, 42, 255};
short COVERED_PINK_LIGHT[3] = {102, 61, 102};
short COVERED_PINK_DARK[3] = {77, 46, 77};
short UNCOVERED_PINK_LIGHT[3] = {179, 107, 179};
short UNCOVERED_PINK_DARK[3] = {153, 92, 153};
short RED[3] = {255, 0, 0};

int option_offset = 20;
int numbuffer_len = 9;

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
  printf("\e[2J");
  printf("\e[?25h");
  printf("\e[u");
}

unsigned long get_size(xyzq_int size) {
  return size.x*size.y*size.z*size.q;
}

void print_xyzq(xyzq_int p) {
  fprintf(stderr, "xyzq_int:\n\tx: %d\n\ty: %d\n\tz: %d\n\ta: %d\n", p.x, p.y, p.z, p.q);
}

uint32_t rand_between(MTRand r, int min, int max) {
  return genRandLong(&r)%(max-min)+min;
}

xyzq_int rand_coord(grid* g, MTRand r) {
  xyzq_int p;
  p.x = genRand(&r)*g->size.x;
  p.y = genRand(&r)*g->size.y;
  p.z = genRand(&r)*g->size.z;
  p.q = genRand(&r)*g->size.q;
}

unsigned long grid_pos(grid* g, xyzq_int p) {
  return ((p.x+(p.y*g->size.x))*g->size.z+p.z)*g->size.q+p.q;
}

short grid_at(grid* g, xyzq_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.q < g->size.q) {
    unsigned long pos = grid_pos(g, p);
    return g->grid[pos];
  } else {
    return -2;
  }
}

void grid_place_at(grid* g, xyzq_int p, short v) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.q < g->size.q) {
    unsigned long pos = grid_pos(g, p);
    g->grid[pos] = v;
    int numwidth = log10(v);
    if (numwidth > 1) {
      g->display_width = numwidth+1;
    }
  }
}

void grid_inc_if_above_at(grid* g, xyzq_int p, short threshold) {
  unsigned long pos = grid_pos(g, p);
  if (g->grid[pos] > threshold) {
    g->grid[pos] += 1;
    int numwidth = log10(g->grid[pos]);
    if (numwidth > 1) {
      g->display_width = numwidth+1;
    }
  }
}

void place_mines(grid* g, MTRand r) {
  for (long i = 0; i < g->bombs; i++) {
    short t = 1;
    while (t) {
      xyzq_int p;
      p.x = genRand(&r)*g->size.x;
      p.y = genRand(&r)*g->size.y;
      p.z = genRand(&r)*g->size.z;
      p.q = genRand(&r)*g->size.q;
      short v = grid_at(g, p);
      if (v > BOMB) {
        grid_place_at(g, p, BOMB);
        t = 0;
        p.x -= 1;
        p.y -= 1;
        p.z -= 1;
        p.q -= 1;
        for (int q = 0; q < 3; q++) {
          if (p.q+q >= 0 && p.q+q < g->size.q) {
            for (int z = 0; z < 3; z++) {
              if (p.z+z >= 0 && p.z+z < g->size.z) {
                for (int y = 0; y < 3; y++) {
                  if (p.y+y >= 0 && p.y+y < g->size.y) {
                    for (int x = 0; x < 3; x++) {
                      if (p.x+x >= 0 && p.x+x < g->size.x) {
                        grid_inc_if_above_at(g, (xyzq_int){p.x+x, p.y+y, p.z+z, p.q+q}, BOMB);
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
        xyzq_int p;
        p.x = genRand(&r)*g->size.x;
        p.y = genRand(&r)*g->size.y;
        p.z = genRand(&r)*g->size.z;
        p.q = genRand(&r)*g->size.q;
        short v = grid_at(g, p);
        if (v == BOMB) {
          unsigned int bombs_in_area = 0;
          grid_place_at(g, p, 0);
          t = 0;
          p.x -= 1;
          p.y -= 1;
          p.z -= 1;
          p.q -= 1;
          for (int q = 0; q < 3; q++) {
            if (p.q+q >= 0 && p.q+q < g->size.q) {
              for (int z = 0; z < 3; z++) {
                if (p.z+z >= 0 && p.z+z < g->size.z) {
                  for (int y = 0; y < 3; y++) {
                    if (p.y+y >= 0 && p.y+y < g->size.y) {
                      for (int x = 0; x < 3; x++) {
                        if (p.x+x >= 0 && p.x+x < g->size.x) {
                          unsigned long pos = grid_pos(g, (xyzq_int){p.x+x, p.y+y, p.z+z, p.q+q});
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
          grid_place_at(g, (xyzq_int){p.x+1, p.y+1, p.z+1, p.q+1}, bombs_in_area);
        }
      }
    }
  }
}

grid* create_grid(xyzq_int size, unsigned long bombs, unsigned int seed, unsigned int recusion_depth) {
  unsigned long len = get_size(size);
  grid* g = (grid*)malloc(sizeof(grid)+len*sizeof(short)*2);
  g->size = size;
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
  g->recusion_depth = recusion_depth;
  g->mask = &(g->grid[len]);
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
  if (n < 10) {
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

xy_int xyzq_int_to_xy_int(grid* g, xyzq_int p) {
  xy_int p2;
  p2.x = (p.z*(g->size.x+1)+p.x)*g->display_width+1;
  p2.y = p.q*(g->size.y+1)+p.y+1;
  return p2;
}

void print_info(grid* g) {
  printf("\e[0m");
  xy_int p;
  p.x = (g->size.z*(g->size.x+1))*g->display_width+2;
  p.y = 2;
  print_str_at(p, "Game info:");
  p.y += 1;
  print_at(p); printf("Seed: %d", g->seed);
  p.y += 1;
  print_at(p); printf("Fields uncovered: %d/%d", g->uncovered, g->len-g->bombs);
  p.y += 1;
  print_at(p); printf("Bombs flagged: %d/%d", g->flagged, g->bombs);
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
    case CLICKED_BOMB:
      printf("Game over");
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

void* update_time_elapsed(void* args) {
  timer* t = args;
  while (1) {
    print_at(t->pos);
    printf("\e[0mTime elapsed: %d seconds", (time(0)-t->g->paused)+t->g->offset);
    //fprintf(stderr, "Time elapsed: %d seconds", (time(0)-t->g->paused)+t->g->offset);
    sleep(1);
  }
}

void print_paused_time_elapsed(grid* g, xy_int pos) {
  print_at(pos);
  printf("\e[0m");
  printf("\e[K");
  printf("Time elapsed: %d seconds", g->offset);
}

void print_field(grid* g, xyzq_int p) {
  set_fg_colour(BLACK);
  unsigned int print_buffer = (g->display_width)/2;
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.q < g->size.q) {
    unsigned long pos = grid_pos(g, p);
    xy_int p2 = xyzq_int_to_xy_int(g, p);
    if (g->mask[pos] == UNCOVERED) {
      if (g->grid[pos] == BOMB) {
        print_wide_str_with_buffer(g, p2, "💣", print_buffer);
        //repeat_str_at(p2, ' ', print_buffer-1);
        //printf("💣%*c", g->display_width-print_buffer-1, ' ');
      } else {
        if (g->grid[pos]) {
          print_short_with_buffer(g, p2, g->grid[pos], print_buffer);
        } else {
          repeat_str_at(p2, ' ', g->display_width);
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

void set_checkered_bg(grid* g, xyzq_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.q < g->size.q) {
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

void print_grid(grid* g) {
  set_fg_colour(BLACK);
  printf("\e[0m");
  printf("\e[2J");
  for (int q = 0; q < g->size.q; q++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzq_int p = {x, y, z, q};
          set_checkered_bg(g, p);
          print_field(g, p);
        }
      }
    }
  }
}

void uncover_field(grid* g, xyzq_int p);

void uncover_field(grid* g, xyzq_int p) {
  if (g->recusion_depth) {
    unsigned long pos = grid_pos(g, p);
    if (g->mask[pos] == COVERED) {
      g->mask[pos] = UNCOVERED;
      g->uncovered += 1;
      set_checkered_bg(g, p);
      print_field(g, p);
      if (g->grid[pos] == BOMB) {
        g->state = CLICKED_BOMB;
      } else if (g->grid[pos] == 0) {
        unsigned int recusion_depth = g->recusion_depth;
        g->recusion_depth -= 1;
        p.x -= 1;
        p.y -= 1;
        p.z -= 1;
        p.q -= 1;
        for (int q = 0; q < 3; q++) {
          if (p.q+q >= 0 && p.q+q < g->size.q) {
            for (int z = 0; z < 3; z++) {
              if (p.z+z >= 0 && p.z+z < g->size.z) {
                for (int y = 0; y < 3; y++) {
                  if (p.y+y >= 0 && p.y+y < g->size.y) {
                    for (int x = 0; x < 3; x++) {
                      if (p.x+x >= 0 && p.x+x < g->size.x) {
                        xyzq_int p2 = {p.x+x, p.y+y, p.z+z, p.q+q};
                        pos = grid_pos(g, p2);
                        if (g->mask[pos] == COVERED) {
                          uncover_field(g, p2);
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        g->recusion_depth = recusion_depth;
      }
      if (g->uncovered == g->len-g->bombs && g->state != CLICKED_BOMB) {
        g->state = WIN;
      }
    }
  }
}

short find_empty(grid* g) {
  for (int q = 0; q < g->size.q; q++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzq_int p = {x, y, z, q};
          if (grid_at(g, p) == 0) {
            uncover_field(g, p);
            return 0;
          }
        }
      }
    }
  }
  return 1;
}

short find_biggest_number(grid* g) {
  short bn = 0;
  for (int q = 0; q < g->size.q; q++) {
    for (int z = 0; z < g->size.z; z++) {
      for (int y = 0; y < g->size.y; y++) {
        for (int x = 0; x < g->size.x; x++) {
          xyzq_int p = {x, y, z, q};
          short v = grid_at(g, p);
          if (v > bn) {
            bn = v;
          }
        }
      }
    }
  }
  return log10(bn)+1;
}

void mark_field(grid* g, xyzq_int p) {
  if (p.x < g->size.x && p.y < g->size.y && p.z < g->size.z && p.q < g->size.q) {
    unsigned long pos = grid_pos(g, p);
    if (g->mask[pos] > COVERED) {
      g->mask[pos] -= 2; //unflag
      g->flagged -= 1;
    } else {
      g->mask[pos] += 2; //flag
      g->flagged += 1;
    }
  }
}

void print_cursor(grid* g, xyzq_int p) {
  set_bg_colour(PINK);
  print_field(g, p);
}

void print_area_of_influence(grid* g, xyzq_int p) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.q -= 1;
  for (int q = 0; q < 3; q++) {
    if (p.q+q >= 0 && p.q+q < g->size.q) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzq_int p2 = {p.x+x, p.y+y, p.z+z, p.q+q};
                  unsigned long pos = grid_pos(g, p2);
                  if ((p2.x+p2.y%2)%2) {
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
                  print_field(g, p2);
                }
              }
            }
          }
        }
      }
    }
  }
}

void remove_cursor(grid* g, xyzq_int p) {
  set_checkered_bg(g, p);
  print_field(g, p);
}

void remove_area_of_influence(grid* g, xyzq_int p) {
  p.x -= 1;
  p.y -= 1;
  p.z -= 1;
  p.q -= 1;
  for (int q = 0; q < 3; q++) {
    if (p.q+q >= 0 && p.q+q < g->size.q) {
      for (int z = 0; z < 3; z++) {
        if (p.z+z >= 0 && p.z+z < g->size.z) {
          for (int y = 0; y < 3; y++) {
            if (p.y+y >= 0 && p.y+y < g->size.y) {
              for (int x = 0; x < 3; x++) {
                if (p.x+x >= 0 && p.x+x < g->size.x) {
                  xyzq_int p2 = {p.x+x, p.y+y, p.z+z, p.q+q};
                  set_checkered_bg(g, p2);
                  print_field(g, p2);
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
        xyzq_int size = {op[1].value, op[2].value, op[3].value, op[4].value};
        print_xyzq(size);
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
  move_terminal_cursor_down(); printf("  Move right in x:     \e[3mright arrow\e[0m, l");
  move_terminal_cursor_down(); printf("  Move left in x:      \e[3mleft arrow\e[0m, h");
  move_terminal_cursor_down(); printf("  Move up in y:        \e[3mup arrow\e[0m, k");
  move_terminal_cursor_down(); printf("  Move down in y:      \e[3mdown arrow\e[0m, j");
  move_terminal_cursor_down(); printf("  Move right in z:     d, ctrl-l");
  move_terminal_cursor_down(); printf("  Move left in z:      a, ctrl-h");
  move_terminal_cursor_down(); printf("  Move up in q:        w, ctrl-k");
  move_terminal_cursor_down(); printf("  Move down in y:      s, ctrl-j");
  move_terminal_cursor_down(); printf("  Mark bomb:           m");
  move_terminal_cursor_down(); printf("  Uncover field:       \e[3mspace\e[0m");
  move_terminal_cursor_down(); printf("  Find empty field:    f");
  move_terminal_cursor_down(); printf("  Turn on delta mode:  u");
  move_terminal_cursor_down(); printf("  Pause game:          p");
  move_terminal_cursor_down(); printf("  Open options:        o");
  move_terminal_cursor_down(); printf("  Start new game:      n");
  move_terminal_cursor_down(); printf("  Print controls:      c");
  move_terminal_cursor_down(); printf("  Toggle info:         i");
  move_terminal_cursor_down(); printf("  Quit game:           q");
}

void print_help_menu() {
  move_terminal_cursor_down();
  move_terminal_cursor_down();
  printf("-h, -?, --help         Show this menu");
  move_terminal_cursor_down();
  printf("-d, --do_random        If true, sets the seed to the current time");
  move_terminal_cursor_down();
  printf("-s, --seed             Input seed as unsigned integer");
  move_terminal_cursor_down();
  printf("-b, --bombs            Input amount of bombs as unsigned integer");
  move_terminal_cursor_down();
  printf("-r, --recursion_depth  The amount of recursion allowed when uncovering fields");
  move_terminal_cursor_down();
  printf("-a, --area, --size     Size of the game (must be given as a comma separated list of unsigned integers e.g 4, 4, 4, 4)");
  move_terminal_cursor_down();
  printf("-i, --show_info        Show info about the current game. Can be set to true or false");
  move_terminal_cursor_down();
  printf("-g, --debug            Run in debug mode. Allows editing field contents");
}

void exit_game_failure() {
  printf("\e[0m");
  printf("\e[?25h");
  exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
  unsigned int seed = time(0);
  short do_random = 1;
  xyzq_int size = {4, 4, 4, 4};
  unsigned int bombs = 20;
  unsigned int recursion_depth = 1000;
  short show_info = 1;
  short debug_mode = 0;
  char numbuffer[numbuffer_len];
  numbuffer[0] = '\0';
  unsigned int numbufferpos = 0;
  printf("\e[?25l");
  printf("\e[s");
  set_input_mode();
  setlocale(LC_CTYPE, "");
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
        size.q = strtol(argv[i], &endptr, 10);
        if (endptr == argv[i]) {
          printf("No digits were found in option %s %s for size q.\n", og, argv[i]);
          exit_game_failure();
        } else if (size.q < 0) {
          printf("Negative size q of %d was provided!\nPlease provide a positive integer", size.q);
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
      } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
        debug_mode = 1;
      }
    }
  }
  option op[11] = {
    {1, 0, 0, EMPTY, "Size"},
    {1, size.x, 2, UINT, "x"},
    {1, size.y, 2, UINT, "y"},
    {1, size.z, 2, UINT, "z"},
    {1, size.q, 2, UINT, "q"},
    {1, do_random, 0, BOOL, "Do random"},
    {1, seed, 2, UINT, "Seed"},
    {1, bombs, 0, UINT, "Bombs"},
    {1, recursion_depth, 0, UINT, "Recusion depth"},
    {1, show_info, 0, BOOL, "Show info"},
    {0, 0, 0, DONE, ""},
  };

  grid* g = create_grid(size, op[7].value, (op[5].value)?time(0):op[6].value, op[8].value);
  unsigned int display_width = g->display_width;
  print_grid(g);
  xyzq_int cursor = {0, 0, 0, 0};
  print_area_of_influence(g, cursor);
  print_cursor(g, cursor);
  if (op[9].value) print_info(g);
  timer t = {(xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7}, g};
  pthread_t timerthread;
  if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
  while (1) {
    int c = getchar();
    remove_area_of_influence(g, cursor);
    remove_cursor(g, cursor);
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
        display_width = find_biggest_number(g);
        g->display_width = display_width;
        print_grid(g);
        if (op[9].value) print_info(g);
        t.pos = (xy_int){(g->size.z*(g->size.x+1))*g->display_width+2, 7};
        fprintf(stderr, "%d\n", g->display_width);
      }
    }
    switch (c) {
      case 68: //right arrow
      case 104: //h
        if (cursor.x-1 >= 0) {
          cursor.x -= 1;
        }
        break;
      case 67: //left arrow
      case 108: //l
        if (cursor.x+1 < g->size.x) {
          cursor.x += 1;
        }
        break;
      case 66: //down arrow
      case 106: //j
        if (cursor.y+1 < g->size.y) {
          cursor.y += 1;
        }
        break;
      case 65: //up arrow
      case 107: //k
        if (cursor.y-1 >= 0) {
          cursor.y -= 1;
        }
        break;
      case 97: //a
      case 8: //ctrl+h
        if (cursor.z-1 >= 0) {
          cursor.z -= 1;
        }
        break;
      case 100: //d
      case 12: //ctrl+l
        if (cursor.z+1 < g->size.z) {
          cursor.z += 1;
        }
        break;
      case 115: //s
      case 10: //ctrl+j
        if (cursor.q+1 < g->size.q) {
          cursor.q += 1;
        }
        break;
      case 119: //w
      case 11: //ctrl+k
        if (cursor.q-1 >= 0) {
          cursor.q -= 1;
        }
        break;
      case 32: //*space*
        uncover_field(g, cursor);
        if (op[9].value) print_info(g);
        break;
      case 109: //m
        mark_field(g, cursor);
        if (op[9].value) print_info(g);
        break;
      case 112: //p
        if (g->state == RUNNING) {
          g->state = PAUSED;
          g->offset += time(0)-g->paused;
          if (op[9].value) {
            pthread_cancel(timerthread);
            print_paused_time_elapsed(g, t.pos);
          }
        } else if (g->state == PAUSED) {
          g->state = RUNNING;
          g->paused = time(0);
          if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        }
        if (op[9].value) print_info(g);
        break;
      case 111: //o
        g->state = PAUSED;
        g->offset += time(0)-g->paused;
        if (op[9].value) pthread_cancel(timerthread);
        g = print_settings(op, g);
        g->state = RUNNING;
        g->paused = time(0);
        if (op[9].value) pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        print_grid(g);
        if (op[9].value) print_info(g);
        break;
      case 110: //n
        free(g);
        size = (xyzq_int){op[1].value, op[2].value, op[3].value, op[4].value};
        g = create_grid(size, op[7].value, (op[5].value)?time(0):op[6].value, op[8].value);
        print_grid(g);
        if (op[9].value) print_info(g);
        break;
      case 99: //c
        g->state = PAUSED;
        g->offset = time(0)-g->paused;
        if (op[9].value) pthread_cancel(timerthread);
        printf("\e[0m");
        printf("\e[2J");
        print_at((xy_int){2, 2});
        print_controls();
        getchar();
        g->state = RUNNING;
        g->paused = time(0);
        print_grid(g);
        if (op[9].value) {
          print_info(g);
          pthread_create(&timerthread, NULL, update_time_elapsed, &t);
        }
        break;
      case 102: //f
        if (g->uncovered == 0) {
          if (find_empty(g)) {
            print_str_at((xy_int){t.pos.x, 9}, "\e[0mDidn't find any empty fields");
          }
        }
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
        break;
      case 113: //q
        exit_game();
        free(g);
        exit(EXIT_SUCCESS);
    }
    print_area_of_influence(g, cursor);
    print_cursor(g, cursor);
    if (g->state > PAUSED) {
      g->offset = time(0)-g->paused;
      if (op[9].value) {
        pthread_cancel(timerthread);
        print_paused_time_elapsed(g, t.pos);
      }
    }
  }
}
