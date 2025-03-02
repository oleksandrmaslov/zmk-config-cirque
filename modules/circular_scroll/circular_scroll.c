#include <zephyr.h>
#include <zmk/input_processor.h>
#include <zmk/pointing.h>
#include <logging/log.h>
#include <behaviors/mouse_keys.dtsi>  // Подключаем определения для мыши
#include <stdlib.h>

LOG_MODULE_REGISTER(trackpad_circular_scroll, CONFIG_ZMK_LOG_LEVEL);

/*
 * Состояние для отслеживания предыдущего псевдо‑угла движения трекпада.
 * Диапазон: 0 .. 4096, где 4096 соответствует 360°.
 */
struct trackpad_scroll_state {
    bool active;
    uint16_t prev_angle;
};

static struct trackpad_scroll_state tp_state = {
    .active = false,
    .prev_angle = 0,
};

/*
 * Вычисление псевдо‑угла на основе относительных движений dx и dy.
 * Возвращает значение от 0 до 4096.
 */
static inline uint16_t compute_pseudo_angle(int16_t dx, int16_t dy) {
    int16_t adx = abs(dx);
    int16_t ady = abs(dy);
    uint16_t sum = adx + ady;
    if (sum == 0) {
        return 0;
    }
    /* Вычисляем отношение dy к сумме в диапазоне 0..1024 */
    uint16_t ratio = (ady * 1024U) / sum;
    uint16_t angle;
    if (dx >= 0 && dy >= 0) {
        angle = ratio; // 0 .. 1024
    } else if (dx < 0 && dy >= 0) {
        angle = 1024U + (1024U - ratio); // 1024 .. 2048
    } else if (dx < 0 && dy < 0) {
        angle = 2048U + ratio; // 2048 .. 3072
    } else { // dx >= 0 && dy < 0
        angle = 3072U + (1024U - ratio); // 3072 .. 4096
    }
    return angle;
}

/*
 * Обработка события указателя (trackpad).
 * При значительном движении вычисляется псевдо‑угол,
 * затем разница между текущим и предыдущим углом преобразуется в величину скролла.
 */
static int trackpad_scroll_process(struct zmk_input_event *event, void *arg) {
    if (event->type != ZMK_INPUT_EVENT_TYPE_POINTING) {
        return 0;
    }
    struct zmk_pointing_event *pevent = &event->pointing;
    int16_t dx = pevent->dx;
    int16_t dy = pevent->dy;
    
    /* Игнорируем малые движения (dead zone) */
    int32_t mag_sq = dx * dx + dy * dy;
    const int32_t dead_zone_sq = 25; // 5² = 25
    if (mag_sq < dead_zone_sq) {
        tp_state.active = false;
        return 0;
    }
    
    uint16_t angle = compute_pseudo_angle(dx, dy);
    if (!tp_state.active) {
        tp_state.active = true;
        tp_state.prev_angle = angle;
        return 0;
    }
    
    /* Вычисляем разницу углов с учетом wrap-around */
    int16_t delta = (int16_t)angle - (int16_t)tp_state.prev_angle;
    if (delta > 2048) {
        delta -= 4096;
    } else if (delta < -2048) {
        delta += 4096;
    }
    tp_state.prev_angle = angle;
    
    /* Преобразуем изменение угла в величину скролла.
     * Подбирайте коэффициент (здесь 1/1024) экспериментально для нужной чувствительности.
     */
    int scroll_value = (delta * 1) / 1024;
    
    /* Генерируем событие скролла: сбрасываем горизонтальное смещение,
     * устанавливаем вертикальное равным scroll_value, и меняем тип события.
     */
    pevent->dx = 0;
    pevent->dy = scroll_value;
    pevent->type = ZMK_POINTING_EVENT_SCROLL;
    
    return 1; // Событие модифицировано
}

static int trackpad_scroll_init(const struct device *dev) {
    tp_state.active = false;
    tp_state.prev_angle = 0;
    return 0;
}

/* Регистрируем input processor с именем "trackpad_circular_scroll" */
ZMK_INPUT_PROCESSOR_DEFINE(trackpad_circular_scroll,
                             trackpad_scroll_process,
                             trackpad_scroll_init);
