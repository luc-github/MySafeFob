/**
 * @file battery_icon.cpp
 * @brief MySafeFob Factory — voir battery_icon.h. Deuxieme (et dernier a ce
 *        jour) point de contact avec FreeInkUI dans la factory, apres le
 *        splash (splash.cpp) — toujours la copie figee de
 *        components/freeinkui/ (ADR-010 amende).
 */
#include "battery_icon.h"

extern "C" {
#include "gfx.h"
#include "hw_config.h"
}

#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>

using namespace freeink::ui;

void battery_icon_draw(int x, int y, int w, int h, uint8_t percent, bool charging)
{
    DisplayTarget target(gfx_framebuffer(), EINK_W, EINK_H, EINK_WB, Orientation::Portrait);
    const DeviceContext device = target.deviceContext();

    static InteractionBuffer<1> interactions;
    Frame<1> frame(target, device, InputSnapshot{}, interactions);

    BatteryIndicatorProps props;
    props.percent = percent;
    props.charging = charging;
    props.style = BatteryIndicatorStyle::Icon;
    props.color = Color::Black;
    props.label = nullptr;   /* texte du pourcentage : gfx_draw_string, pas ici */
    /* BUGFIX 2026-09-16 : glyphWidth/glyphHeight (defaut 22x11) pilotent la
     * taille REELLE du glyphe, independamment du Rect — agrandir seulement
     * le Rect ne changeait rien (juste plus de marge autour d'une icone
     * minuscule). Il faut fixer explicitement la taille voulue ici. */
    props.glyphWidth = static_cast<int16_t>(w);
    props.glyphHeight = static_cast<int16_t>(h);

    /* Rect = juste assez large pour glyphe + nub (+2px), aligne a droite
     * dedans -> body colle a (x,y) comme avant. */
    const int16_t rectW = static_cast<int16_t>(w + 4);
    batteryIndicator(frame, Rect{static_cast<int16_t>(x), static_cast<int16_t>(y),
                                 rectW, static_cast<int16_t>(h)},
                     props);
}
