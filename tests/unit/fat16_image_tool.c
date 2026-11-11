/* Exercise the production FAT16 writer on a disposable host disk image. */
#include "../../ssos/os/fs/storage.h"

#include <stdio.h>
#include <stdint.h>

#define SECTOR_SIZE 512u

static int read_sector(void* context, uint32_t sector, uint8_t* out) {
    FILE* file = context;
    return fseek(file, (long)sector * SECTOR_SIZE, SEEK_SET) ||
           fread(out, 1, SECTOR_SIZE, file) != SECTOR_SIZE ? -1 : 0;
}

static int write_sector(void* context, uint32_t sector, const uint8_t* in) {
    FILE* file = context;
    return fseek(file, (long)sector * SECTOR_SIZE, SEEK_SET) ||
           fwrite(in, 1, SECTOR_SIZE, file) != SECTOR_SIZE ? -1 : 0;
}

int main(int argc, char** argv) {
    static uint8_t fresh[7001], replacement[12003], check[12003], streamed[70001];
    if (argc != 2) return 1;
    FILE* file = fopen(argv[1], "r+b");
    if (!file) return 1;
    if (fseek(file, 0, SEEK_END)) return 1;
    long image_size = ftell(file);
    if (image_size <= 0 || image_size % SECTOR_SIZE || fseek(file, 0, SEEK_SET)) return 1;
    for (unsigned i = 0; i < sizeof(fresh); i++) fresh[i] = (uint8_t)(i % 251u);
    for (unsigned i = 0; i < sizeof(replacement); i++)
        replacement[i] = (uint8_t)((i * 7u + 13u) % 251u);
    for (unsigned i = 0; i < sizeof(streamed); i++) streamed[i] = (uint8_t)(i % 251u);
    SSStorage storage = {0};
    int failed = ss_storage_mount(&storage, read_sector, write_sector, file,
                                  (uint32_t)(image_size / SECTOR_SIZE), SECTOR_SIZE) ||
                 ss_storage_store(&storage, "FRESH.BIN", fresh, sizeof(fresh), 0) ||
                 ss_storage_store(&storage, "EXIST.BIN", replacement,
                                  sizeof(replacement), 1) ||
                 ss_storage_read(&storage, "EXIST.BIN", check, sizeof(check)) !=
                     (int32_t)sizeof(check);
    for (unsigned i = 0; !failed && i < sizeof(check); i++)
        if (check[i] != replacement[i]) failed = 1;
    SSFat16Writer writer;
    if (!failed && (ss_storage_writer_begin(&storage, "STREAM.BIN", 0, &writer) ||
        ss_storage_writer_write(&storage, &writer, streamed, 333) != 333 ||
        ss_storage_writer_write(&storage, &writer, streamed + 333, 65536) != 65536 ||
        ss_storage_writer_write(&storage, &writer, streamed + 65869,
                                sizeof(streamed) - 65869) != (int32_t)(sizeof(streamed) - 65869) ||
        ss_storage_writer_end(&storage, &writer, 1))) failed = 1;
    if (fclose(file)) failed = 1;
    return failed ? 1 : 0;
}
