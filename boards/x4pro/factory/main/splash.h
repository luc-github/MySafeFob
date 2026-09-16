/* 
 Project: MySafeFob  splash.h
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
 * @file splash.h
 * @brief MySafeFob Factory — static splash at boot (ADR-010 amended
 *   2026-09-15: FreeInkUI::DisplayTarget, frozen copy specific to the factory,
 *   see components/freeinkui/CMakeLists.txt).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draws and displays the splash (resources/splash.png, converted by
 *        tools/gen_splash.py) with a full refresh. eink_init() must
 *        already have been called.
 */
void splash_show(void);

#ifdef __cplusplus
}
#endif
