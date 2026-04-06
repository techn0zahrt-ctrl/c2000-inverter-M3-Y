/*
 * This file is part of the c2000-inverter project.
 *
 * Copyright (C) 2025 openinverter.org
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef C2000_CAN_H
#define C2000_CAN_H

#include "canhardware.h"
#include "canmap.h"
#include <stdint.h>

class C2000Can : public CanHardware
{
public:
   C2000Can(uint32_t canBase);
   void SetBaudrate(enum baudrates baudrate) override;
   void Send(uint32_t canId, uint32_t data[2], uint8_t len) override;
   void HandleMessage();
   int GetUserMessageCount() { return nextUserMessageIndex; }
   uint32_t GetUserId(int i) { return userIds[i]; }
   uint32_t* GetUserIdPtr() { return userIds; }
   
   /* Public so ISR free functions can access them without friend/extern-C clash */
   static C2000Can* instanceA;
   static C2000Can* instanceB;

protected:
   void ConfigureFilters() override;
   void RegisterInterrupts() override;

private:
   uint32_t base;
};

extern "C" __interrupt void canAISR(void);
extern "C" __interrupt void canBISR(void);

#endif // C2000_CAN_H
