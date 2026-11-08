// A virtual keyboard for the TSP: creates /dev/uinput and takes key commands
// from the FIFO /tmp/re4keys ("SPACE 150", one per line: key hold ms).
// Start it before the game so SDL's evdev keyboard finds it at init.
#include <linux/uinput.h>
#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static int code(const char* n)
{
    if (!strcmp(n, "SPACE")) return KEY_SPACE;
    if (!strcmp(n, "ENTER")) return KEY_ENTER;
    if (!strcmp(n, "UP")) return KEY_UP;
    if (!strcmp(n, "DOWN")) return KEY_DOWN;
    if (!strcmp(n, "LEFT")) return KEY_LEFT;
    if (!strcmp(n, "RIGHT")) return KEY_RIGHT;
    if (!strcmp(n, "ESC")) return KEY_ESC;
    if (!strcmp(n, "Z")) return KEY_Z;
    if (!strcmp(n, "X")) return KEY_X;
    if (!strcmp(n, "C")) return KEY_C;
    if (!strcmp(n, "V")) return KEY_V;
    if (!strcmp(n, "E")) return KEY_E;
    if (!strcmp(n, "A")) return KEY_A;
    if (!strcmp(n, "S")) return KEY_S;
    if (!strcmp(n, "D")) return KEY_D;
    if (!strcmp(n, "W")) return KEY_W;
    if (!strcmp(n, "BACKSPACE")) return KEY_BACKSPACE;
    return -1;
}

static void emit(int fd, int type, int c, int v)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = c;
    ev.value = v;
    write(fd, &ev, sizeof ev);
}

int main(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    const int keys[] = {KEY_SPACE, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESC, KEY_Z,
                        KEY_X, KEY_C, KEY_V, KEY_E, KEY_A, KEY_S, KEY_D, KEY_W, KEY_BACKSPACE};
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    struct uinput_user_dev u;
    memset(&u, 0, sizeof u);
    snprintf(u.name, sizeof u.name, "re4-inject");
    u.id.bustype = BUS_USB;
    u.id.vendor = 0x1234;
    u.id.product = 0x5678;
    write(fd, &u, sizeof u);
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    fprintf(stderr, "re4keys: ready\n");
    mkfifo("/tmp/re4keys", 0666);
    for (;;) {
        FILE* f = fopen("/tmp/re4keys", "r");
        if (!f) { usleep(200000); continue; }
        char line[64];
        while (fgets(line, sizeof line, f)) {
            char name[32];
            int ms = 120;
            if (sscanf(line, "%31s %d", name, &ms) < 1) continue;
            int c = code(name);
            if (c < 0) continue;
            emit(fd, EV_KEY, c, 1);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            usleep(ms * 1000);
            emit(fd, EV_KEY, c, 0);
            emit(fd, EV_SYN, SYN_REPORT, 0);
            usleep(80000);
        }
        fclose(f);
    }
}
