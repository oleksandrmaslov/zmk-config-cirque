#include <zephyr.h>
#include <device.h>
#include <zmk/input_processor.h>
#include <zmk/pointing.h>
#include <logging/log.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(circular_scroll, CONFIG_ZMK_LOG_LEVEL);

/*
 * Состояние для хранения предыдущего псевдо-угла.
 * Псевдо-угол вычисляется в диапазоне 0..4096, где 4096 соответствует 360°.
 */
struct circular_scroll_state {
    bool active;
    uint16_t prev_angle;
};

static struct circular_scroll_state cs_state = {
    .active = false,
    .prev_angle = 0,
};

/*
 * Функция pseudo_angle вычисляет приближённый угол движения без использования операций с плавающей точкой.
 * Алгоритм:
 *  - Вычисляем сумму абсолютных значений dx и dy (чем больше сумма, тем точнее результат).
 *  - Вычисляем ratio = (|dy| * 1024) / (|dx| + |dy|).
 *  - В зависимости от знаков dx и dy возвращаем угол в диапазоне 0..4096.
 *
 * Диапазон разбит следующим образом:
 *   0     .. 1024: 1-я четверть (dx>=0, dy>=0)
 *   1024  .. 2048: 2-я четверть (dx<0, dy>=0)
 *   2048  .. 3072: 3-я четверть (dx<0, dy<0)
 *   3072  .. 4096: 4-я четверть (dx>=0, dy<0)
 */
static inline uint16_t pseudo_angle(int16_t dx, int16_t dy) {
    int16_t ax = abs(dx), ay = abs(dy);
    uint16_t sum = ax + ay;
    if (sum == 0) {
        return 0;
    }
    uint16_t ratio = (ay * 1024U) / sum; // ratio от 0 до 1024
    uint16_t angle;
    if (dx >= 0 && dy >= 0) {
        angle = ratio; // 0 .. 1024
    } else if (dx < 0 && dy >= 0) {
        angle = 1024U + (1024U - ratio); // 1024 .. 2048
    } else if (dx < 0 && dy < 0) {
        angle = 2048U + ratio; // 2048 .. 3072
    } else { /* dx >= 0 && dy < 0 */
        angle = 3072U + (1024U - ratio); // 3072 .. 4096
    }
    return angle;
}

/*
 * Функция обработки событий указателя.
 * Использует псевдо-угол для вычисления изменения направления (delta) и преобразует его в событие прокрутки.
 */
static int circular_scroll_process(struct zmk_input_event *event, void *arg) {
    if (event->type != ZMK_INPUT_EVENT_TYPE_POINTING) {
        return 0;
    }

    struct zmk_pointing_event *pevent = &event->pointing;
    int16_t dx = pevent->dx;
    int16_t dy = pevent->dy;

    /* Используем порог по квадрату величины, чтобы игнорировать незначительные движения */
    int32_t mag_sq = dx * dx + dy * dy;
    const int32_t dead_zone_sq = 25; // например, порог 5 единиц: 5^2 = 25
    if (mag_sq < dead_zone_sq) {
        cs_state.active = false;
        return 0;
    }

    /* Вычисляем псевдо-угол движения */
    uint16_t angle = pseudo_angle(dx, dy);

    if (!cs_state.active) {
        cs_state.active = true;
        cs_state.prev_angle = angle;
        return 0;
    }

    /* Вычисляем разницу псевдо-углов с учетом перехода через границу (wrap-around) */
    int16_t delta = (int16_t)angle - (int16_t)cs_state.prev_angle;
    if (delta > 2048) {
        delta -= 4096;
    } else if (delta < -2048) {
        delta += 4096;
    }
    cs_state.prev_angle = angle;

    /*
     * Преобразуем изменение угла в величину прокрутки.
     * Коэффициент масштабирования (например, *10/1024) можно настроить под требуемую чувствительность.
     */
    int scroll_value = (delta * 10) / 1024;

    /* Модифицируем событие:
     * - Обнуляем горизонтальное смещение,
     * - Устанавливаем вертикальное смещение равным рассчитанному значению прокрутки,
     * - Если поддерживается, меняем тип события на scroll.
     */
    pevent->dx = 0;
    pevent->dy = scroll_value;
    pevent->type = ZMK_POINTING_EVENT_SCROLL;

    return 1; // событие было модифицировано
}

static int circular_scroll_init(const struct device *dev) {
    cs_state.active = false;
    cs_state.prev_angle = 0;
    return 0;
}

/* Регистрируем input processor под именем "circular_scroll" */
ZMK_INPUT_PROCESSOR_DEFINE(circular_scroll, circular_scroll_process, circular_scroll_init);
