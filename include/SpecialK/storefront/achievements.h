/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/

#pragma once

#ifndef __SK__ACHIEVEMENTS_H__
#define __SK__ACHIEVEMENTS_H__

class SK_AchievementManager
{
public:
  void loadSound (const wchar_t* wszUnlockSound);

protected:
  bool           default_loaded = false;
  uint8_t*       unlock_sound   = nullptr;   // A .WAV (PCM) file
};

#endif /* __SK__ACHIEVEMENTS_H__ */