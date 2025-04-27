#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

int main() {
    // This will shift the cursor right a few places
    printf("some text");

    struct termios restore;
    tcgetattr(0, &restore);

    // Disable flags in order to read the response
    struct termios term;
    tcgetattr(0, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &term);

    // Write ANSI escape sequence for cursor position
    write(1, "\033[6n", 4);

    // Read back response
    char buffer[16] = { 0 };
    int idx = 0;
    char ch;
    while (ch != 'R') {
        read(0, &ch, 1);
        buffer[idx] = ch;
        ++idx;
    }
    buffer[idx] = '\0';

    // Restore original settings
    tcsetattr(0, TCSANOW, &restore);

    // +1 because the first character is ESC
    puts(buffer + 1);

    return 0;
}

