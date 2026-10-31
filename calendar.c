/**
 * Flipper Zero Calendar Application
 *
 * Original author: TechArtDev (https://github.com/techartdev/Calendar)
 * Co-author: Taras Zakharchenko / player999 (https://github.com/player999/flipper-calendar)
 * License: MIT
 */

#include "calendar.i.h"
#include <ctype.h>

#define MAX_HOLIDAY_EVENTS 256
#define MAX_HOLIDAY_NAME_LEN 32

#define CALENDAR_SETTINGS_FILE APP_DATA_PATH("calendar.conf")
#define CALENDAR_SETTINGS_FILETYPE "Flipper Calendar Configuration"
#define CALENDAR_SETTINGS_VERSION 1

typedef enum {
    CalendarViewMain = 0,
    CalendarViewSettings = 1,
} CalendarViewId;

typedef enum {
    CalendarLangEN = 0,
    CalendarLangUK = 1,
    CalendarLangPL = 2,
    CalendarLangDE = 3,
} CalendarLanguage;

typedef enum {
    CalendarDateFormatEU = 0,  // DD.MM.YY
    CalendarDateFormatISO = 1, // YYYY-MM-DD
    CalendarDateFormatUS = 2,  // MM/DD/YY
} CalendarDateFormat;

typedef struct {
    CalendarLanguage language;
    bool show_workweek;
    bool first_day_monday;
    CalendarDateFormat date_format;
} CalendarSettings;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    char name[MAX_HOLIDAY_NAME_LEN];
} HolidayEvent;

typedef struct {
    HolidayEvent events[MAX_HOLIDAY_EVENTS];
    size_t count;
    bool loaded;
} HolidayCalendar;

typedef struct {
    int selected_month;
    int selected_year;
    int current_day;
    int current_month;
    int current_year;
    int selected_holiday_idx;
    size_t scroll_counter;
    FuriString* holiday_str;
    HolidayCalendar* holidays;
    CalendarSettings* settings;
} CalendarData;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* calendar_view;
    VariableItemList* var_item_list;
    VariableItem* item_lang;
    VariableItem* item_ww;
    VariableItem* item_first_day;
    VariableItem* item_date_fmt;
    CalendarData* calendar_data;
    CalendarSettings settings;
    uint32_t current_view;
} CalendarApp;

static void clean_string(char* dst, const char* src, size_t max_len) {
    size_t j = 0;
    for(size_t i = 0; src[i] != '\0' && j < max_len - 1; i++) {
        if((unsigned char)src[i] == 0xe2 && (unsigned char)src[i + 1] == 0x80 &&
           (unsigned char)src[i + 2] == 0x99) {
            dst[j++] = '\'';
            i += 2;
        } else if((unsigned char)src[i] >= 32 && (unsigned char)src[i] <= 126) {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static void calendar_sort_holidays(HolidayCalendar* cal) {
    if(cal->count < 2) return;
    for(size_t i = 1; i < cal->count; i++) {
        HolidayEvent key = cal->events[i];
        int j = (int)i - 1;
        while(j >= 0) {
            int cmp = 0;
            if(cal->events[j].year != key.year) {
                cmp = (int)cal->events[j].year - (int)key.year;
            } else if(cal->events[j].month != key.month) {
                cmp = (int)cal->events[j].month - (int)key.month;
            } else if(cal->events[j].day != key.day) {
                cmp = (int)cal->events[j].day - (int)key.day;
            } else {
                cmp = strcmp(cal->events[j].name, key.name);
            }
            if(cmp <= 0) break;
            cal->events[j + 1] = cal->events[j];
            j--;
        }
        cal->events[j + 1] = key;
    }
}

static bool calendar_has_event(
    HolidayCalendar* cal,
    uint16_t year,
    uint8_t month,
    uint8_t day,
    const char* name) {
    for(size_t i = 0; i < cal->count; i++) {
        if(cal->events[i].year == year &&
           cal->events[i].month == month &&
           cal->events[i].day == day &&
           strcmp(cal->events[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void calendar_parse_single_ics_file(
    Storage* storage,
    const char* path,
    HolidayCalendar* cal) {
    Stream* stream = buffered_file_stream_alloc(storage);
    if(!stream) return;

    if(!buffered_file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        stream_free(stream);
        return;
    }

    FuriString* line = furi_string_alloc();
    bool in_event = false;
    uint16_t ev_year = 0;
    uint8_t ev_month = 0;
    uint8_t ev_day = 0;
    char ev_name[MAX_HOLIDAY_NAME_LEN] = {0};

    while(stream_read_line(stream, line)) {
        furi_string_trim(line);
        const char* cstr = furi_string_get_cstr(line);
        if(strlen(cstr) == 0) continue;

        if(strcmp(cstr, "BEGIN:VEVENT") == 0) {
            in_event = true;
            ev_year = 0;
            ev_month = 0;
            ev_day = 0;
            ev_name[0] = '\0';
        } else if(strcmp(cstr, "END:VEVENT") == 0) {
            if(in_event && ev_year > 0 && ev_month > 0 && ev_day > 0) {
                if(cal->count < MAX_HOLIDAY_EVENTS &&
                   !calendar_has_event(cal, ev_year, ev_month, ev_day, ev_name)) {
                    cal->events[cal->count].year = ev_year;
                    cal->events[cal->count].month = ev_month;
                    cal->events[cal->count].day = ev_day;
                    clean_string(cal->events[cal->count].name, ev_name, MAX_HOLIDAY_NAME_LEN);
                    cal->count++;
                }
            }
            in_event = false;
        } else if(in_event) {
            if(strncmp(cstr, "DTSTART", 7) == 0) {
                const char* colon = strchr(cstr, ':');
                if(colon) {
                    const char* val = colon + 1;
                    if(strlen(val) >= 8 &&
                       isdigit((unsigned char)val[0]) && isdigit((unsigned char)val[1]) &&
                       isdigit((unsigned char)val[2]) && isdigit((unsigned char)val[3]) &&
                       isdigit((unsigned char)val[4]) && isdigit((unsigned char)val[5]) &&
                       isdigit((unsigned char)val[6]) && isdigit((unsigned char)val[7])) {
                        char yb[5] = {val[0], val[1], val[2], val[3], '\0'};
                        char mb[3] = {val[4], val[5], '\0'};
                        char db[3] = {val[6], val[7], '\0'};
                        ev_year = atoi(yb);
                        ev_month = atoi(mb);
                        ev_day = atoi(db);
                    }
                }
            } else if(strncmp(cstr, "SUMMARY", 7) == 0) {
                const char* colon = strchr(cstr, ':');
                if(colon) {
                    clean_string(ev_name, colon + 1, sizeof(ev_name));
                }
            }
        }
    }

    furi_string_free(line);
    buffered_file_stream_close(stream);
    stream_free(stream);
}

static bool calendar_scan_ics_directory(
    Storage* storage,
    const char* dir_path,
    HolidayCalendar* cal) {
    File* dir = storage_file_alloc(storage);
    if(!dir) return false;

    if(!storage_dir_open(dir, dir_path)) {
        storage_dir_close(dir);
        storage_file_free(dir);
        return false;
    }

    FileInfo fileinfo;
    char name[64];
    char full_path[128];
    bool found_any = false;

    while(storage_dir_read(dir, &fileinfo, name, sizeof(name))) {
        if(!file_info_is_dir(&fileinfo)) {
            size_t len = strlen(name);
            if(len >= 4) {
                const char* ext = name + len - 4;
                if(ext[0] == '.' &&
                   (ext[1] == 'i' || ext[1] == 'I') &&
                   (ext[2] == 'c' || ext[2] == 'C') &&
                   (ext[3] == 's' || ext[3] == 'S')) {
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);
                    calendar_parse_single_ics_file(storage, full_path, cal);
                    found_any = true;
                }
            }
        }
    }

    storage_dir_close(dir);
    storage_file_free(dir);
    return found_any;
}

static void calendar_load_ics(HolidayCalendar* cal) {
    if(!cal) return;
    cal->count = 0;
    cal->loaded = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return;

    bool scanned = false;
    if(calendar_scan_ics_directory(storage, APP_DATA_PATH(""), cal)) {
        scanned = true;
    }
    if(!scanned || cal->count == 0) {
        calendar_scan_ics_directory(storage, "/ext/apps_data/techart_calendar", cal);
    }

    if(cal->count == 0) {
        const char* fallback_paths[] = {
            APP_DATA_PATH("poland.ics"),
            "/ext/apps_data/techart_calendar/poland.ics",
            APP_DATA_PATH("calendar.ics"),
            "/ext/apps_data/techart_calendar/calendar.ics",
            "/ext/poland.ics",
        };
        for(size_t p = 0; p < sizeof(fallback_paths) / sizeof(fallback_paths[0]); p++) {
            calendar_parse_single_ics_file(storage, fallback_paths[p], cal);
            if(cal->count > 0) break;
        }
    }

    furi_record_close(RECORD_STORAGE);

    calendar_sort_holidays(cal);
    cal->loaded = (cal->count > 0);
}

static uint32_t calendar_get_month_holiday_mask(HolidayCalendar* cal, int year, int month) {
    if(!cal) return 0;
    uint32_t mask = 0;
    for(size_t i = 0; i < cal->count; i++) {
        if(cal->events[i].year == year && cal->events[i].month == month) {
            if(cal->events[i].day >= 1 && cal->events[i].day <= 31) {
                mask |= (1UL << cal->events[i].day);
            }
        }
    }
    return mask;
}

static int calendar_count_month_holidays(HolidayCalendar* cal, int year, int month) {
    if(!cal) return 0;
    int count = 0;
    for(size_t i = 0; i < cal->count; i++) {
        if(cal->events[i].year == year && cal->events[i].month == month) {
            count++;
        }
    }
    return count;
}

static const HolidayEvent* calendar_get_month_holiday_by_index(
    HolidayCalendar* cal,
    int year,
    int month,
    int index) {
    if(!cal || index < 0) return NULL;
    int match = 0;
    for(size_t i = 0; i < cal->count; i++) {
        if(cal->events[i].year == year && cal->events[i].month == month) {
            if(match == index) {
                return &cal->events[i];
            }
            match++;
        }
    }
    return NULL;
}

static int calculateDayOfWeek(int year, int month, int day) {
    if(month < 3) {
        month += 12;
        year -= 1;
    }

    int K = year % 100;
    int J = year / 100;

    int h = (day + (13 * (month + 1) / 5) + K + (K / 4) + (J / 4) + (5 * J)) % 7;
    int dayOfWeek = (h + 6) % 7;

    return dayOfWeek;
}

static DateTime createDateTime(uint16_t year, uint8_t month, uint8_t day) {
    DateTime date = {0, 0, 0, day, month, year, 0};
    date.weekday = calculateDayOfWeek(year, month, day);
    return date;
}

static int calculate_workweek(int year, int month, int day) {
    int dow = calculateDayOfWeek(year, month, day);
    int sat_day = day + (6 - dow);
    int sat_month = month;
    int sat_year = year;
    int days_in_cur_month = datetime_get_days_per_month(datetime_is_leap_year(year), month);

    if(sat_day > days_in_cur_month) {
        sat_day -= days_in_cur_month;
        sat_month++;
        if(sat_month > 12) {
            sat_month = 1;
            sat_year++;
        }
    }

    int jan1_dow = calculateDayOfWeek(sat_year, 1, 1);
    int first_sat_day = 1 + (6 - jan1_dow);

    int sat_doy = sat_day;
    for(int m = 1; m < sat_month; m++) {
        sat_doy += datetime_get_days_per_month(datetime_is_leap_year(sat_year), m);
    }

    int ww = 1 + (sat_doy - first_sat_day) / 7;
    return ww;
}

static void format_numeric_date(
    char* dst,
    size_t max_len,
    int year,
    int month,
    int day,
    CalendarDateFormat fmt) {
    int yy = year % 100;
    switch(fmt) {
    case CalendarDateFormatEU: // DD.MM.YY
        snprintf(dst, max_len, "%02d.%02d.%02d", day, month, yy);
        break;
    case CalendarDateFormatISO: // YYYY-MM-DD
        snprintf(dst, max_len, "%04d-%02d-%02d", year, month, day);
        break;
    case CalendarDateFormatUS: // MM/DD/YY
        snprintf(dst, max_len, "%02d/%02d/%02d", month, day, yy);
        break;
    default:
        snprintf(dst, max_len, "%02d.%02d.%02d", day, month, yy);
        break;
    }
}

// 3x5 compact bitmap font with multilingual headers
typedef struct {
    char ch;
    uint8_t rows[5];
} MiniGlyph;

static const MiniGlyph mini_font[] = {
    {'0', {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'1', {0b010, 0b110, 0b010, 0b010, 0b111}},
    {'2', {0b111, 0b001, 0b111, 0b100, 0b111}},
    {'3', {0b111, 0b001, 0b111, 0b001, 0b111}},
    {'4', {0b101, 0b101, 0b111, 0b001, 0b001}},
    {'5', {0b111, 0b100, 0b111, 0b001, 0b111}},
    {'6', {0b111, 0b100, 0b111, 0b101, 0b111}},
    {'7', {0b111, 0b001, 0b010, 0b010, 0b010}},
    {'8', {0b111, 0b101, 0b111, 0b101, 0b111}},
    {'9', {0b111, 0b101, 0b111, 0b001, 0b111}},
    {'W', {0b101, 0b101, 0b101, 0b111, 0b101}},
    {'S', {0b111, 0b100, 0b111, 0b001, 0b111}},
    {'M', {0b101, 0b111, 0b101, 0b101, 0b101}},
    {'T', {0b111, 0b010, 0b010, 0b010, 0b010}},
    {'F', {0b111, 0b100, 0b110, 0b100, 0b100}},
    {'D', {0b110, 0b101, 0b101, 0b101, 0b110}},
    {'N', {0b101, 0b111, 0b101, 0b101, 0b101}},
    {'P', {0b111, 0b101, 0b111, 0b100, 0b100}},
    {'V', {0b101, 0b101, 0b101, 0b101, 0b010}},
    {'C', {0b111, 0b100, 0b100, 0b100, 0b111}},
    {'-', {0b000, 0b000, 0b111, 0b000, 0b000}},
};

static void draw_mini_char(Canvas* canvas, int x, int y, char c) {
    const uint8_t* rows = NULL;
    for(size_t i = 0; i < sizeof(mini_font) / sizeof(mini_font[0]); i++) {
        if(mini_font[i].ch == c) {
            rows = mini_font[i].rows;
            break;
        }
    }
    if(!rows) return;

    for(int r = 0; r < 5; r++) {
        uint8_t b = rows[r];
        if(b & 0b100) canvas_draw_dot(canvas, x + 0, y + r);
        if(b & 0b010) canvas_draw_dot(canvas, x + 1, y + r);
        if(b & 0b001) canvas_draw_dot(canvas, x + 2, y + r);
    }
}

static void draw_mini_str(Canvas* canvas, int x, int y, const char* str) {
    int cur_x = x;
    while(*str) {
        if(*str == ' ') {
            cur_x += 2;
        } else {
            draw_mini_char(canvas, cur_x, y, *str);
            cur_x += 4;
        }
        str++;
    }
}

static const char* const month_names[4][12] = {
    // English
    {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"},
    // Ukrainian
    {"Sichen'", "Lyutyi", "Berezen'", "Kviten'", "Traven'", "Cherven'", "Lypen'", "Serpen'", "Veresen'", "Zhovten'", "Lystopad", "Hruden'"},
    // Polish
    {"Styczen", "Luty", "Marzec", "Kwiecien", "Maj", "Czerwiec", "Lipiec", "Sierpien", "Wrzesien", "Pazdziernik", "Listopad", "Grudzien"},
    // German
    {"Januar", "Februar", "Maerz", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"},
};

static const char* const month_short_names[4][12] = {
    // English
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
    // Ukrainian
    {"Sich", "Lyut", "Ber", "Kvit", "Trav", "Cher", "Lyp", "Serp", "Ver", "Zhov", "Lyst", "Hrud"},
    // Polish
    {"Sty", "Lut", "Mar", "Kwi", "Maj", "Cze", "Lip", "Sie", "Wrz", "Paz", "Lis", "Gru"},
    // German
    {"Jan", "Feb", "Mae", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"},
};

static const char* const label_today[4] = {"Today:", "Sohodni:", "Dzisiaj:", "Heute:"};
static const char* const label_weeks[4] = {"Weeks:", "Tyzhni:", "Tygodnie:", "Wochen:"};

static const char* const day_headers_mon[4][7] = {
    {"M", "T", "W", "T", "F", "S", "S"},
    {"P", "V", "S", "C", "P", "S", "N"},
    {"P", "W", "S", "C", "P", "S", "N"},
    {"M", "D", "M", "D", "F", "S", "S"},
};

static const char* const day_headers_sun[4][7] = {
    {"S", "M", "T", "W", "T", "F", "S"},
    {"N", "P", "V", "S", "C", "P", "S"},
    {"N", "P", "W", "S", "C", "P", "S"},
    {"S", "M", "D", "M", "D", "F", "S"},
};

static void calendar_draw_callback(Canvas* canvas, void* model) {
    CalendarData* calendar_data = *(CalendarData**)model;
    if(!calendar_data || !calendar_data->settings) return;

    CalendarSettings* settings = calendar_data->settings;
    bool show_ww = settings->show_workweek;
    bool mon_first = settings->first_day_monday;
    int lang = settings->language;

    canvas_clear(canvas);
    canvas_set_bitmap_mode(canvas, true);

    DateTime date = createDateTime(calendar_data->selected_year, calendar_data->selected_month, 1);
    int days_in_month = datetime_get_days_per_month(datetime_is_leap_year(date.year), date.month);

    int first_col = mon_first ? ((date.weekday + 6) % 7) : date.weekday;
    int total_rows = (first_col + days_in_month + 6) / 7;

    // Top Bar: Month & Year + Exit arrow
    canvas_set_font(canvas, FontSecondary);
    char title_buf[32];
    snprintf(
        title_buf,
        sizeof(title_buf),
        "< %s %d >",
        month_names[lang][calendar_data->selected_month - 1],
        calendar_data->selected_year);
    canvas_draw_str(canvas, 2, 9, title_buf);
    canvas_draw_icon(canvas, 116, 1, &I_Pin_back_arrow_10x8);

    // Separator line under header
    canvas_draw_line(canvas, 0, 11, 127, 11);

    // Grid coordinates
    const int gx = 1;
    const int gy = 13;
    const int col_w = 9;
    const int row_h = 7;
    const int grid_bottom = gy + (total_rows + 1) * row_h;
    const int total_cols = show_ww ? 8 : 7;

    // Horizontal separator under day names
    canvas_draw_line(canvas, gx, gy + row_h, gx + total_cols * col_w, gy + row_h);

    // Vertical separator between WW column and days (only if WW is shown)
    if(show_ww) {
        canvas_draw_line(canvas, gx + col_w, gy, gx + col_w, grid_bottom);
    }

    // Column headers (3x5 font)
    const char* const* headers = mon_first ? day_headers_mon[lang] : day_headers_sun[lang];
    if(show_ww) {
        draw_mini_str(canvas, gx + 3, gy + 1, "W");
        for(int c = 0; c < 7; c++) {
            draw_mini_str(canvas, gx + (c + 1) * col_w + 3, gy + 1, headers[c]);
        }
    } else {
        for(int c = 0; c < 7; c++) {
            draw_mini_str(canvas, gx + c * col_w + 3, gy + 1, headers[c]);
        }
    }

    int day_of_month = 1;
    char str_buf[32];

    bool is_current_month = (calendar_data->selected_year == calendar_data->current_year &&
                             calendar_data->selected_month == calendar_data->current_month);

    uint32_t holiday_mask = calendar_get_month_holiday_mask(
        calendar_data->holidays,
        calendar_data->selected_year,
        calendar_data->selected_month);

    int month_holidays = calendar_count_month_holidays(
        calendar_data->holidays,
        calendar_data->selected_year,
        calendar_data->selected_month);

    const HolidayEvent* focused_holiday = calendar_get_month_holiday_by_index(
        calendar_data->holidays,
        calendar_data->selected_year,
        calendar_data->selected_month,
        calendar_data->selected_holiday_idx);

    int first_row_ww = 0;
    int last_row_ww = 0;

    for(int i = 0; i < total_rows; i++) {
        int cell_y = gy + (i + 1) * row_h + 1;

        if(show_ww) {
            int row_day = 1 + i * 7 - first_col;
            if(row_day < 1) row_day = 1;
            if(row_day > days_in_month) row_day = days_in_month;
            int ww = calculate_workweek(calendar_data->selected_year, calendar_data->selected_month, row_day);
            if(i == 0) first_row_ww = ww;
            last_row_ww = ww;

            snprintf(str_buf, sizeof(str_buf), "%d", ww);
            int ww_x = (ww < 10) ? (gx + 3) : (gx + 1);
            draw_mini_str(canvas, ww_x, cell_y, str_buf);
        }

        // Draw days of this row
        for(int j = 0; j < 7; j++) {
            if(i == 0 && j < first_col) {
                continue;
            }
            if(day_of_month > days_in_month) {
                break;
            }

            int day_col_index = show_ww ? (j + 1) : j;
            int col_x = gx + day_col_index * col_w;
            int day_x = (day_of_month < 10) ? (col_x + 3) : (col_x + 1);

            snprintf(str_buf, sizeof(str_buf), "%d", day_of_month);

            bool is_holiday = (holiday_mask & (1UL << day_of_month)) != 0;
            bool is_today = (is_current_month && day_of_month == calendar_data->current_day);
            bool is_focused_holiday = (focused_holiday && day_of_month == focused_holiday->day);

            if(is_holiday || is_today) {
                canvas_draw_box(canvas, col_x, cell_y - 1, 9, 7);
                canvas_invert_color(canvas);
                draw_mini_str(canvas, day_x, cell_y, str_buf);
                canvas_invert_color(canvas);

                if((is_today && !is_holiday) || is_focused_holiday) {
                    canvas_invert_color(canvas);
                    canvas_draw_dot(canvas, col_x, cell_y - 1);
                    canvas_draw_dot(canvas, col_x + 8, cell_y - 1);
                    canvas_draw_dot(canvas, col_x, cell_y + 5);
                    canvas_draw_dot(canvas, col_x + 8, cell_y + 5);
                    canvas_invert_color(canvas);
                }
            } else {
                draw_mini_str(canvas, day_x, cell_y, str_buf);
            }

            day_of_month++;
        }
    }

    // Right Side Information Card: adjust x & width based on whether WW is shown
    int card_x = show_ww ? 75 : 67;
    int card_w = 127 - card_x;
    canvas_draw_rframe(canvas, card_x, 13, card_w, 50, 2);

    int text_x = card_x + 3;
    int text_w = card_w - 6;

    if(focused_holiday) {
        canvas_set_font(canvas, FontSecondary);
        char str_date[20];
        format_numeric_date(
            str_date,
            sizeof(str_date),
            calendar_data->selected_year,
            calendar_data->selected_month,
            focused_holiday->day,
            settings->date_format);
        int date_w = canvas_string_width(canvas, str_date);
        int date_x = card_x + (card_w - date_w) / 2;
        if(date_x < card_x + 2) date_x = card_x + 2;
        canvas_draw_str(canvas, date_x, 23, str_date);

        // Scrolling holiday name (marquee)
        furi_string_set(calendar_data->holiday_str, focused_holiday->name);
        elements_scrollable_text_line(
            canvas,
            text_x,
            38,
            text_w,
            calendar_data->holiday_str,
            calendar_data->scroll_counter,
            false);

        // Footer action hint with counter
        if(month_holidays > 1) {
            snprintf(
                str_buf,
                sizeof(str_buf),
                "OK: %d/%d >",
                calendar_data->selected_holiday_idx + 1,
                month_holidays);
            canvas_draw_str(canvas, text_x, 58, str_buf);
        } else {
            canvas_draw_str(canvas, text_x, 58, is_current_month ? "OK: today" : "OK: close");
        }
    } else if(is_current_month) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, text_x, 23, label_today[lang]);

        char str_date[20];
        format_numeric_date(
            str_date,
            sizeof(str_date),
            calendar_data->current_year,
            calendar_data->current_month,
            calendar_data->current_day,
            settings->date_format);
        int date_w = canvas_string_width(canvas, str_date);
        int date_x = card_x + (card_w - date_w) / 2;
        if(date_x < card_x + 2) date_x = card_x + 2;
        canvas_draw_str(canvas, date_x, 35, str_date);

        if(show_ww) {
            int today_ww = calculate_workweek(
                calendar_data->current_year, calendar_data->current_month, calendar_data->current_day);
            snprintf(str_buf, sizeof(str_buf), "WW%d", today_ww);
            canvas_draw_str(canvas, text_x, 47, str_buf);
        } else {
            canvas_draw_str(canvas, text_x, 47, month_short_names[lang][calendar_data->current_month - 1]);
        }

        if(month_holidays > 0) {
            snprintf(str_buf, sizeof(str_buf), "OK: %d hol", month_holidays);
            canvas_draw_str(canvas, text_x, 58, str_buf);
        } else {
            canvas_draw_str(canvas, text_x, 58, "Hold OK: Set");
        }
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, text_x, 23, label_weeks[lang]);

        if(show_ww) {
            canvas_set_font(canvas, FontPrimary);
            snprintf(str_buf, sizeof(str_buf), "%d-%d", first_row_ww, last_row_ww);
            canvas_draw_str(canvas, text_x, 35, str_buf);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, text_x, 47, month_short_names[lang][calendar_data->selected_month - 1]);
        } else {
            canvas_draw_str(canvas, text_x, 35, month_names[lang][calendar_data->selected_month - 1]);
            char year_buf[16];
            snprintf(year_buf, sizeof(year_buf), "%d", calendar_data->selected_year);
            canvas_draw_str(canvas, text_x, 47, year_buf);
        }

        if(month_holidays > 0) {
            snprintf(str_buf, sizeof(str_buf), "OK: %d hol", month_holidays);
            canvas_draw_str(canvas, text_x, 58, str_buf);
        } else {
            canvas_draw_str(canvas, text_x, 58, "Hold OK: Set");
        }
    }
}

static const char* const setting_lang_names[] = {"English", "Ukrainian", "Polish", "German"};
static const char* const setting_ww_names[] = {"ON", "OFF"};
static const char* const setting_day_names[] = {"Monday", "Sunday"};
static const char* const setting_fmt_names[] = {"EU", "ISO", "US"};

static void calendar_settings_change_callback(VariableItem* item) {
    CalendarApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    if(item == app->item_lang) {
        app->settings.language = index;
        variable_item_set_current_value_text(item, setting_lang_names[index]);
    } else if(item == app->item_ww) {
        app->settings.show_workweek = (index == 0);
        variable_item_set_current_value_text(item, setting_ww_names[index]);
    } else if(item == app->item_first_day) {
        app->settings.first_day_monday = (index == 0);
        variable_item_set_current_value_text(item, setting_day_names[index]);
    } else if(item == app->item_date_fmt) {
        app->settings.date_format = index;
        variable_item_set_current_value_text(item, setting_fmt_names[index]);
    }
}

static void calendar_settings_save(CalendarSettings* settings) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_always(ff, CALENDAR_SETTINGS_FILE)) break;
        if(!flipper_format_write_header_cstr(ff, CALENDAR_SETTINGS_FILETYPE, CALENDAR_SETTINGS_VERSION)) break;
        uint32_t val = settings->language;
        if(!flipper_format_write_uint32(ff, "Language", &val, 1)) break;
        val = settings->show_workweek ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "ShowWorkweek", &val, 1)) break;
        val = settings->first_day_monday ? 1 : 0;
        if(!flipper_format_write_uint32(ff, "FirstDayMonday", &val, 1)) break;
        val = settings->date_format;
        if(!flipper_format_write_uint32(ff, "DateFormat", &val, 1)) break;
    } while(0);
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void calendar_settings_load(CalendarSettings* settings) {
    settings->language = CalendarLangEN;
    settings->show_workweek = false;
    settings->first_day_monday = true;
    settings->date_format = CalendarDateFormatISO;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_existing(ff, CALENDAR_SETTINGS_FILE)) break;
        uint32_t val = 0;
        if(flipper_format_read_uint32(ff, "Language", &val, 1)) {
            if(val <= CalendarLangDE) settings->language = val;
        }
        if(flipper_format_read_uint32(ff, "ShowWorkweek", &val, 1)) {
            settings->show_workweek = (val != 0);
        }
        if(flipper_format_read_uint32(ff, "FirstDayMonday", &val, 1)) {
            settings->first_day_monday = (val != 0);
        }
        if(flipper_format_read_uint32(ff, "DateFormat", &val, 1)) {
            if(val <= CalendarDateFormatUS) settings->date_format = val;
        }
    } while(0);
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void calendar_settings_init(CalendarApp* app) {
    app->var_item_list = variable_item_list_alloc();

    app->item_lang = variable_item_list_add(
        app->var_item_list,
        "Language",
        4,
        calendar_settings_change_callback,
        app);
    variable_item_set_current_value_index(app->item_lang, app->settings.language);
    variable_item_set_current_value_text(app->item_lang, setting_lang_names[app->settings.language]);

    app->item_ww = variable_item_list_add(
        app->var_item_list,
        "Workweek",
        2,
        calendar_settings_change_callback,
        app);
    uint8_t ww_idx = app->settings.show_workweek ? 0 : 1;
    variable_item_set_current_value_index(app->item_ww, ww_idx);
    variable_item_set_current_value_text(app->item_ww, setting_ww_names[ww_idx]);

    app->item_first_day = variable_item_list_add(
        app->var_item_list,
        "Week Start",
        2,
        calendar_settings_change_callback,
        app);
    uint8_t day_idx = app->settings.first_day_monday ? 0 : 1;
    variable_item_set_current_value_index(app->item_first_day, day_idx);
    variable_item_set_current_value_text(app->item_first_day, setting_day_names[day_idx]);

    app->item_date_fmt = variable_item_list_add(
        app->var_item_list,
        "Date",
        3,
        calendar_settings_change_callback,
        app);
    variable_item_set_current_value_index(app->item_date_fmt, app->settings.date_format);
    variable_item_set_current_value_text(app->item_date_fmt, setting_fmt_names[app->settings.date_format]);
}

static bool calendar_view_input_callback(InputEvent* event, void* context) {
    CalendarApp* app = context;
    CalendarData* calendar_data = app->calendar_data;

    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        variable_item_list_set_selected_item(app->var_item_list, 0);
        view_dispatcher_switch_to_view(app->view_dispatcher, CalendarViewSettings);
        app->current_view = CalendarViewSettings;
        return true;
    }

    if(event->type == InputTypeShort) {
        calendar_data->scroll_counter = 0;
        if(event->key == InputKeyLeft) {
            calendar_data->selected_holiday_idx = -1;
            calendar_data->selected_month--;
            if(calendar_data->selected_month < 1) {
                if(calendar_data->selected_year > 2000) {
                    calendar_data->selected_month = 12;
                    calendar_data->selected_year--;
                } else {
                    calendar_data->selected_month = 1;
                }
            }
            view_commit_model(app->calendar_view, true);
            return true;
        } else if(event->key == InputKeyRight) {
            calendar_data->selected_holiday_idx = -1;
            calendar_data->selected_month++;
            if(calendar_data->selected_month > 12) {
                if(calendar_data->selected_year < 2099) {
                    calendar_data->selected_month = 1;
                    calendar_data->selected_year++;
                } else {
                    calendar_data->selected_month = 12;
                }
            }
            view_commit_model(app->calendar_view, true);
            return true;
        } else if(event->key == InputKeyUp) {
            calendar_data->selected_holiday_idx = -1;
            if(calendar_data->selected_year < 2099) {
                calendar_data->selected_year++;
            }
            view_commit_model(app->calendar_view, true);
            return true;
        } else if(event->key == InputKeyDown) {
            calendar_data->selected_holiday_idx = -1;
            if(calendar_data->selected_year > 2000) {
                calendar_data->selected_year--;
            }
            view_commit_model(app->calendar_view, true);
            return true;
        } else if(event->key == InputKeyOk) {
            int m_holidays = calendar_count_month_holidays(
                calendar_data->holidays,
                calendar_data->selected_year,
                calendar_data->selected_month);
            if(m_holidays > 0) {
                calendar_data->selected_holiday_idx++;
                if(calendar_data->selected_holiday_idx >= m_holidays) {
                    calendar_data->selected_holiday_idx = -1;
                }
            } else {
                calendar_data->selected_holiday_idx = -1;
                calendar_data->selected_month = calendar_data->current_month;
                calendar_data->selected_year = calendar_data->current_year;
            }
            view_commit_model(app->calendar_view, true);
            return true;
        }
    }

    return false;
}

static bool calendar_navigation_callback(void* context) {
    CalendarApp* app = context;
    if(app->current_view == CalendarViewSettings) {
        calendar_settings_save(&app->settings);
        view_dispatcher_switch_to_view(app->view_dispatcher, CalendarViewMain);
        app->current_view = CalendarViewMain;
        view_commit_model(app->calendar_view, true);
        return true;
    }
    return false;
}

static void calendar_tick_callback(void* context) {
    CalendarApp* app = context;
    if(app->current_view == CalendarViewMain) {
        app->calendar_data->scroll_counter++;
        view_commit_model(app->calendar_view, true);
    }
}

int32_t calendar_app(void* p) {
    UNUSED(p);

    CalendarApp* app = malloc(sizeof(CalendarApp));
    memset(app, 0, sizeof(CalendarApp));

    calendar_settings_load(&app->settings);

    CalendarData* calendar_data = malloc(sizeof(CalendarData));
    memset(calendar_data, 0, sizeof(CalendarData));
    calendar_data->settings = &app->settings;

    HolidayCalendar* holidays = malloc(sizeof(HolidayCalendar));
    memset(holidays, 0, sizeof(HolidayCalendar));
    calendar_load_ics(holidays);
    calendar_data->holidays = holidays;

    DateTime currentDate;
    furi_hal_rtc_get_datetime(&currentDate);

    calendar_data->selected_month = currentDate.month;
    calendar_data->selected_year = currentDate.year;
    calendar_data->current_day = currentDate.day;
    calendar_data->current_month = currentDate.month;
    calendar_data->current_year = currentDate.year;
    calendar_data->selected_holiday_idx = -1;
    calendar_data->holiday_str = furi_string_alloc();

    app->calendar_data = calendar_data;

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();

    // Calendar View
    app->calendar_view = view_alloc();
    view_allocate_model(app->calendar_view, ViewModelTypeLockFree, sizeof(CalendarData*));
    CalendarData** model_ptr = view_get_model(app->calendar_view);
    *model_ptr = calendar_data;
    view_set_draw_callback(app->calendar_view, calendar_draw_callback);
    view_set_input_callback(app->calendar_view, calendar_view_input_callback);
    view_set_context(app->calendar_view, app);

    // Settings View
    calendar_settings_init(app);
    View* settings_view = variable_item_list_get_view(app->var_item_list);

    view_dispatcher_add_view(app->view_dispatcher, CalendarViewMain, app->calendar_view);
    view_dispatcher_add_view(app->view_dispatcher, CalendarViewSettings, settings_view);

    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, calendar_navigation_callback);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, calendar_tick_callback, 250);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, CalendarViewMain);
    app->current_view = CalendarViewMain;

    view_dispatcher_run(app->view_dispatcher);

    // Cleanup
    calendar_settings_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, CalendarViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, CalendarViewSettings);

    view_free(app->calendar_view);
    variable_item_list_free(app->var_item_list);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_string_free(calendar_data->holiday_str);
    free(holidays);
    free(calendar_data);
    free(app);

    return 0;
}
