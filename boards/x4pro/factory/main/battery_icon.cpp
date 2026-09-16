/* 
 Project: MySafeFob  battery_icon.cpp
  Copyright (c) 2026 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/**
 * @file battery_icon.cpp
 * @brief MySafeFob Factory — see battery_icon.h. Second (and so far last)
 *        point of contact with FreeInkUI in the factory, after the
 *        splash (splash.cpp) — always the frozen copy of
 *        components/freeinkui/ (ADR-010 amended).
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
    props.label = nullptr;   /* percentage text: gfx_draw_string, not here */
    /* BUGFIX 2026-09-16: glyphWidth/glyphHeight (default 22x11) drive the
     * ACTUAL glyph size, independent of the Rect — enlarging just the
     * Rect changed nothing (just more margin around a tiny icon). The
     * desired size must be set explicitly here. */
    props.glyphWidth = static_cast<int16_t>(w);
    props.glyphHeight = static_cast<int16_t>(h);

    /* Rect = just wide enough for glyph + nub (+2px), right-aligned
     * inside it -> body sticks to (x,y) as before. */
    const int16_t rectW = static_cast<int16_t>(w + 4);
    batteryIndicator(frame, Rect{static_cast<int16_t>(x), static_cast<int16_t>(y),
                                 rectW, static_cast<int16_t>(h)},
                     props);
}
