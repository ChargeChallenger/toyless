#ifndef TOYLESS_FIGURE_DB_H
#define TOYLESS_FIGURE_DB_H

// Сгенерировано tools/gen_figure_db.py из базы RPCS3
// Всего фигур: 322

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FIGURE_KIND_CHARACTER  = 0, // Круглый персонаж (слоты Игрока 1/2)
    FIGURE_KIND_PLAYSET    = 1, // Шестиугольный плейсет (Центральный слот)
    FIGURE_KIND_ROUND_DISC = 2, // Круглый диск способностей/костюма (под персонажа)
    FIGURE_KIND_HEX_DISC   = 3, // Шестиугольный диск транспорта/окружения (Центр)
} figure_kind_t;

typedef struct {
    uint32_t    toy_id;
    uint8_t     series; // 1, 2, 3
    uint8_t     kind;   // figure_kind_t
    const char *name;
} figure_db_entry_t;

extern const uint16_t figure_db_count;
extern const figure_db_entry_t figure_db_entries[];

// Поиск фигурки по toy_id (возвращает NULL, если не найдена)
const figure_db_entry_t *figure_db_find_by_id(uint32_t toy_id);

// Сжатый GZIP каталог для отправки в браузер через GET /api/catalog
extern const uint8_t  figure_db_catalog_gz[];
extern const uint32_t figure_db_catalog_gz_len;

#endif // TOYLESS_FIGURE_DB_H
