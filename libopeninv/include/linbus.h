/*
 * This file is part of the stm32-car project.
 *
 * Copyright (C) 2021 Johannes Huebner <dev@johanneshuebner.com>
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
#ifndef LINBUS_H
#define LINBUS_H

#include <stdint.h>
#ifdef __TMS320C2000__
#ifndef override
#define override
#endif
#ifndef final
#define final
#endif
typedef uint16_t uint8_t;
typedef int16_t int8_t;
#endif

/** \brief LIN bus interface / STM32 concrete implementation.
 *
 * On STM32F1 this class provides the concrete USART+DMA hardware driver.
 * On other platforms (C2000, host) the hardware methods are no-ops; a
 * platform-specific subclass overrides them with real hardware access.
 */
class LinBus
{
public:
   LinBus();
   LinBus(uint32_t usart, int baudrate);
   virtual ~LinBus() {}

   virtual void Init(uint32_t usart, int baudrate);
   virtual void Request(uint8_t id, uint8_t* data, uint8_t len);
   virtual bool HasReceived(uint8_t id, uint8_t requiredLen);
   virtual uint8_t* GetReceivedBytes() { return &recvBuffer[payloadIndex]; }

   static uint8_t Checksum(uint8_t pid, uint8_t* data, int len);
   static uint8_t Parity(uint8_t id);

protected:
   static const int payloadIndex = 3;
   static const int pidIndex = 2;
   uint8_t sendBuffer[11];
   uint8_t recvBuffer[12];

#ifdef STM32F1
private:
   struct HwInfo
   {
      uint32_t usart;
      uint32_t dma;
      uint8_t  dmatx;
      uint8_t  dmarx;
      uint32_t port;
      uint16_t pintx;
      uint16_t pinrx;
   };
   static const HwInfo hwInfo[];
   const HwInfo* hw;
#endif
};

#endif // LINBUS_H
