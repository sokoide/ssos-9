#include "buffer.h"
#include "ssos9/user.h"

#define TEXT_ROWS 23u
#define TEXT_COLUMNS 79u

typedef struct {
    EditBuffer text;
    const char* name;
    uint32_t top_line;
    uint32_t left_column;
    uint8_t existing;
    uint8_t mode; /* 0 normal, 1 insert, 2 command */
    uint8_t command_length;
    char command[5];
    const char* message;
} Editor;

static uint32_t length(const char* text) {
    uint32_t n = 0;
    while (text[n]) n++;
    return n;
}

static int write_text(const char* text) {
    uint32_t n = length(text);
    return ss9_write(1, text, n) == (int32_t)n ? 0 : -1;
}

static void pause_tick(void) {
#ifdef EDIT_HOST_TEST
    extern void ss9_test_pause(void);
    ss9_test_pause();
#else
    register uint32_t d0 __asm__("d0") = SS9_SYS_SLEEP;
    register uint32_t d1 __asm__("d1") = 1;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
#endif
}

static int next_key(void) {
    int32_t key;
    while ((key = ss9_console_key()) < 0) pause_tick();
    return (int)key;
}

/* An open error also covers I/O errors: do not silently create or replace. */
static int load(Editor* e) {
    int32_t fd = ss9_open_read(e->name);
    if (fd < 0) {
        if (write_text("Cannot read file. n: new, q: quit\r\n")) return -1;
        int key;
        do { key = next_key(); } while (key != 'n' && key != 'q');
        if (key == 'q') return -1;
        e->existing = 0;
        return 0;
    }
    int32_t n = ss9_read((uint32_t)fd, e->text.data, EDIT_CAPACITY);
    uint8_t extra;
    int32_t more = n >= 0 ? ss9_read((uint32_t)fd, &extra, 1) : -1;
    int32_t close_status = ss9_close((uint32_t)fd);
    if (n < 0 || more != 0 || close_status != 0) {
        (void)write_text("Read error or file exceeds 16384 bytes.\r\n");
        return -1;
    }
    e->text.length = (uint32_t)n;
    e->existing = 1;
    return 0;
}

static int save(Editor* e) {
    int32_t fd = ss9_open(e->name, e->existing ? SS9_OPEN_REPLACE : SS9_OPEN_CREATE);
    if (fd < 0) { e->message = "Save failed: open"; return -1; }
    if (ss9_write((uint32_t)fd, e->text.data, e->text.length) != (int32_t)e->text.length) {
        e->message = "Save failed: write";
        return -1;
    }
    if (ss9_close((uint32_t)fd) != 0) { e->message = "Save failed: close"; return -1; }
    e->existing = 1;
    e->text.modified = 0;
    e->message = "Written";
    return 0;
}

static void render(Editor* e) {
    uint32_t line = 0, pos = 0;
    for (uint32_t i = 0; i < e->text.cursor; i++)
        if (e->text.data[i] == '\n') line++;
    if (line < e->top_line) e->top_line = line;
    if (line >= e->top_line + TEXT_ROWS) e->top_line = line - TEXT_ROWS + 1u;
    uint32_t column = e->text.cursor - edit_line_start(&e->text, e->text.cursor);
    if (column < e->left_column) e->left_column = column;
    if (column >= e->left_column + TEXT_COLUMNS)
        e->left_column = column - TEXT_COLUMNS + 1u;
    for (uint32_t n = 0; n < e->top_line && pos < e->text.length; n++) {
        pos = edit_line_end(&e->text, pos);
        if (pos < e->text.length) pos++;
    }
    for (uint16_t row = 0; row < TEXT_ROWS; row++) {
        uint8_t output[TEXT_COLUMNS];
        uint32_t end = edit_line_end(&e->text, pos);
        for (uint32_t x = 0; x < TEXT_COLUMNS; x++) {
            uint32_t at = pos + e->left_column + x;
            uint8_t c = at < end ? e->text.data[at] : ' ';
            output[x] = c >= 32 && c <= 126 ? c : ' ';
        }
        (void)ss9_console_locate(0, row);
        (void)ss9_write(1, output, TEXT_COLUMNS);
        pos = end < e->text.length ? end + 1u : end;
    }
    uint8_t status[TEXT_COLUMNS];
    for (uint32_t i = 0; i < TEXT_COLUMNS; i++) status[i] = ' ';
    const char* mode = e->mode == 1 ? "INSERT" : e->mode == 2 ? "COMMAND" : "NORMAL";
    uint32_t at = 0;
    for (uint32_t i = 0; mode[i] && at < TEXT_COLUMNS; i++) status[at++] = (uint8_t)mode[i];
    status[at++] = ' ';
    if (e->text.modified) status[at++] = '*';
    for (uint32_t i = 0; e->name[i] && at < 40; i++) status[at++] = (uint8_t)e->name[i];
    status[at++] = ' ';
    if (e->message)
        for (uint32_t i = 0; e->message[i] && at < TEXT_COLUMNS; i++) status[at++] = (uint8_t)e->message[i];
    (void)ss9_console_locate(0, 23);
    (void)ss9_write(1, status, TEXT_COLUMNS);
    uint8_t command[TEXT_COLUMNS];
    for (uint32_t i = 0; i < TEXT_COLUMNS; i++) command[i] = ' ';
    if (e->mode == 2) {
        command[0] = ':';
        for (uint32_t i = 0; i < e->command_length; i++) command[i + 1u] = (uint8_t)e->command[i];
    }
    (void)ss9_console_locate(0, 24);
    (void)ss9_write(1, command, TEXT_COLUMNS);
    if (e->mode == 2) (void)ss9_console_locate((uint16_t)(e->command_length + 1u), 24);
    else (void)ss9_console_locate((uint16_t)(column - e->left_column), (uint16_t)(line - e->top_line));
}

static int command(Editor* e) {
    char* c = e->command;
    unsigned n = e->command_length;
    if (n == 1 && c[0] == 'w') { (void)save(e); return 0; }
    if (n == 2 && c[0] == 'w' && c[1] == 'q') return save(e) == 0;
    if (n == 2 && c[0] == 'q' && c[1] == '!') return 1;
    if (n == 1 && c[0] == 'q') {
        if (!e->text.modified) return 1;
        e->message = "Unsaved: use :q!";
        return 0;
    }
    e->message = "Unknown command";
    return 0;
}

int main(int argc, char** argv) {
    Editor e;
    if (argc != 2) { (void)write_text("Usage: run edit FILE\r\n"); return 2; }
    edit_init(&e.text);
    e.name = argv[1];
    e.top_line = e.left_column = 0;
    e.existing = e.mode = e.command_length = 0;
    e.message = 0;
    if (load(&e)) return 1;
    (void)ss9_console_clear();
    for (;;) {
        render(&e);
        int key = next_key();
        e.message = 0;
        if (e.mode == 1) {
            if (key == 27) e.mode = 0;
            else if (key == 8 || key == 127) (void)edit_backspace(&e.text);
            else if (key == '\r' || key == '\n') {
                if (edit_insert(&e.text, '\n')) e.message = "Buffer full";
            } else if ((key >= 32 && key <= 126) || key == '\t') {
                if (edit_insert(&e.text, (uint8_t)key)) e.message = "Buffer full";
            }
        } else if (e.mode == 2) {
            if (key == 27) e.mode = 0;
            else if (key == '\r' || key == '\n') {
                if (command(&e)) break;
                e.mode = 0;
            } else if (key == 8 || key == 127) {
                if (e.command_length) e.command_length--;
            } else if (key >= 32 && key < 127 && e.command_length < sizeof(e.command)) {
                e.command[e.command_length++] = (char)key;
            }
        } else {
            if (key == 'h') edit_left(&e.text);
            else if (key == 'l') edit_right(&e.text);
            else if (key == 'k') edit_up(&e.text);
            else if (key == 'j') edit_down(&e.text);
            else if (key == 'x') (void)edit_delete(&e.text);
            else if (key == 'i') e.mode = 1;
            else if (key == 'a') { edit_right(&e.text); e.mode = 1; }
            else if (key == ':') { e.command_length = 0; e.mode = 2; }
        }
    }
    (void)ss9_console_clear();
    return 0;
}
