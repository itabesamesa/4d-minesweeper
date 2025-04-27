# 4d minesweeper

4d minesweeper made for the terminal

![A screenshot of a game of 4d minesweeper](screenshot1.png)

## How to play

You have to find all the mines in a 4 dimensional field. The pink cursor highlights the current field your on and its value shows the number of bombs that are around it. To help you find which fields are in the area of influence of the current field, they are highlighted in a less saturated shade of pink. Have fun finding all the mines!

![A screenshot if a finished game](screenshot2.png)

### Controls

>  Move right in x:     right arrow, l\
>  Move left in x:      left arrow, h\
>  Move up in y:        up arrow, k\
>  Move down in y:      down arrow, j\
>  Move right in z:     d, ctrl-l\
>  Move left in z:      a, ctrl-h\
>  Move up in q:        w, ctrl-k\
>  Move down in y:      s, ctrl-j\
>  Mark bomb:           m\
>  Uncover field:       space\
>  Find empty fiel:     f\
>  Turn on delta mode:  u\
>  Pause game:          p\
>  Open options:        o\
>  Start new game:      n\
>  Print controls:      c\
>  Quit game:           q

## Compiling and running

To compile the program, simply run:

```
gcc main.c mtwister.c -lm -o 4dminsweeper
```

> [!WARNING]
> This program was made of Linux, i doubt that it would work on windows

To run the program, simply type:

```
./4dminesweeper
```

> [!WARNING]
> Make sure your terminal is big enough!!!

There are a few extra options you could add, if you don't want to edit the settings while the game is running

>  -h, -?, --help         Show this menu\
>  -d, --do_random        If true, sets the seed to the current time\
>  -s, --seed             Input seed as unsigned integer\
>  -b, --bombs            Input amount of bombs as unsigned integer\
>  -r, --recursion_depth  The amount of recursion allowed when uncovering fields\
>  -a, --area, --size     Size of the game (must be given as comma separated list of unsigned integers e.g 4, 4, 4, 4)\
>  -i, --show_info        Show info about the current game. Can be set to true or false

## Missing features

- Delta mode          decrement surrounding numbers when bomb is marked
- Big numbers         currently i don't know what will happen if a field has a 3 digit number
