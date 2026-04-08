/*
 * This file is part of the stm32-... project.
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
#include "cansdo.h"
#include "my_math.h"
#include "errormessage.h"

#ifdef C2000
uint16_t printBuffer[64];
#else
char printBuffer[64];
#endif

#define PRINT_BUF_CAPACITY  ((uint32_t)(sizeof(printBuffer) / sizeof(printBuffer[0])))
#define PRINT_BUF_MASK      (PRINT_BUF_CAPACITY - 1U)

#define SDO_REQ_ID_BASE       0x600U
#define SDO_REP_ID_BASE       0x580U

#define SDO_INDEX_PARAMS      0x2000
#define SDO_INDEX_PARAM_UID   0x2100
#define SDO_INDEX_MAP_TX      0x3000
#define SDO_INDEX_MAP_RX      0x3001
#define SDO_INDEX_MAP_RD      0x3100
#define SDO_INDEX_STRINGS     0x5001
#define SDO_INDEX_ERROR_NUM   0x5003
#define SDO_INDEX_ERROR_TIME  0x5004

#define PRINT_TIMEOUT         1000

static inline void PrintBufEnqueue(volatile uint32_t& in, uint8_t c)
{
   uint32_t i = in;
   in = i + 1U;
#ifdef C2000
   printBuffer[i & PRINT_BUF_MASK] = (uint16_t)c;
#else
   printBuffer[i & PRINT_BUF_MASK] = (char)c;
#endif
}

static inline uint8_t PrintBufDequeue(volatile uint32_t& out)
{
   uint32_t i = out;
   out = i + 1U;
#ifdef C2000
   return (uint8_t)(printBuffer[i & PRINT_BUF_MASK] & 0xFFU);
#else
   return (uint8_t)printBuffer[i & PRINT_BUF_MASK];
#endif
}

static inline bool PrintBufFull(uint32_t in, uint32_t out)
{
   return (in - out) == PRINT_BUF_CAPACITY;
}

static inline bool PrintBufEmpty(uint32_t in, uint32_t out)
{
   return in == out;
}

static inline uint32_t PrintBufDepth(uint32_t in, uint32_t out)
{
   return in - out;
}

uint32_t CanSdo::GetPrintFree() const
{
   uint32_t used = PrintBufDepth(printByteIn, printByteOut);
   if (used >= PRINT_BUF_CAPACITY)
      return 0;
   return PRINT_BUF_CAPACITY - used;
}

bool CanSdo::GetPrintOverflow() const
{
   return printOverflow;
}

void CanSdo::ResetPrintOverflow()
{
   printOverflow = false;
}

void CanSdo::ClearPrintRequest()
{
   printRequest = -1;
}

CanSdo::CanSdo(CanHardware* hw, CanMap* cm)
 : canHardware(hw), canMap(cm), nodeId(1), remoteNodeId(255), printRequest(-1),
   printByteIn(0), printByteOut(0), printTimeout(PRINT_TIMEOUT),
   mapParam(Param::PARAM_INVALID), mapId(0), sdoReplyValid(false), sdoReplyData(0),
   pendingUserSpaceSdo(false), printCallback(0)
{
   canHardware->AddCallback(this);
   HandleClear();
}

void CanSdo::HandleClear()
{
   canHardware->RegisterUserMessage(SDO_REQ_ID_BASE + nodeId);

   if (remoteNodeId < 64)
      canHardware->RegisterUserMessage(SDO_REP_ID_BASE + remoteNodeId);
}

void CanSdo::HandleRx(uint32_t canId, uint32_t data[2], uint8_t)
{
   if (canId == (SDO_REQ_ID_BASE + nodeId))
   {
      ProcessSDO(data);
   }
   else if (canId == (SDO_REP_ID_BASE + remoteNodeId))
   {
#ifdef C2000
      uint16_t rep_cmd   = (uint16_t)(data[0] & 0xFFUL);
      uint16_t rep_index = (uint16_t)(((data[0] >> 8) & 0xFFUL) | (((data[0] >> 16) & 0xFFUL) << 8));
      uint32_t rep_data  = data[1];

      if (rep_index == SDO_INDEX_MAP_RX || rep_index == SDO_INDEX_MAP_TX)
      {
         uint16_t rep_subIndex = (uint16_t)((data[0] >> 24) & 0xFFUL);
         if (rep_subIndex == 0)
            InitiateSDOTransfer(SDO_WRITE, remoteNodeId, rep_index, 1,
                                mapInfo.mapParam |
                                ((uint32_t)mapInfo.offsetBits << 16) |
                                ((uint32_t)mapInfo.numBits << 24));
         else if (rep_subIndex == 1)
            InitiateSDOTransfer(SDO_WRITE, remoteNodeId, rep_index, 2,
                                (int32_t)(mapInfo.gain * 1000.0f) |
                                ((uint32_t)mapInfo.offset << 24));
      }

      sdoReplyValid = rep_cmd != SDO_ABORT;
      sdoReplyData = rep_data;
#else
      SdoFrame* sdoFrame = (SdoFrame*)data;

      if (sdoFrame->index == SDO_INDEX_MAP_RX || sdoFrame->index == SDO_INDEX_MAP_TX)
      {
         if (sdoFrame->subIndex == 0)
            InitiateSDOTransfer(SDO_WRITE, remoteNodeId, sdoFrame->index, 1,
                                mapInfo.mapParam |
                                ((uint32_t)mapInfo.offsetBits << 16) |
                                ((uint32_t)mapInfo.numBits << 24));
         else if (sdoFrame->subIndex == 1)
            InitiateSDOTransfer(SDO_WRITE, remoteNodeId, sdoFrame->index, 2,
                                (int32_t)(mapInfo.gain * 1000.0f) |
                                ((uint32_t)mapInfo.offset << 24));
      }

      sdoReplyValid = sdoFrame->cmd != SDO_ABORT;
      sdoReplyData = sdoFrame->data;
#endif
   }
}

void CanSdo::SDOWrite(uint8_t nodeId, uint16_t index, uint8_t subIndex, uint32_t data)
{
   InitiateSDOTransfer(SDO_WRITE, nodeId, index, subIndex, data);
}

void CanSdo::SDORead(uint8_t nodeId, uint16_t index, uint8_t subIndex)
{
   InitiateSDOTransfer(SDO_READ, nodeId, index, subIndex, 0);
}

bool CanSdo::SDOReadReply(uint32_t& data)
{
   data = sdoReplyData;
   return sdoReplyValid;
}

void CanSdo::RemoteMap(uint8_t nodeId, bool rx, uint32_t cobId, CanMap::CANPOS mapping)
{
   mapInfo = mapping;
   InitiateSDOTransfer(SDO_WRITE, nodeId, rx ? SDO_INDEX_MAP_RX : SDO_INDEX_MAP_TX, 0, cobId);
}

void CanSdo::SetNodeId(uint8_t id)
{
   nodeId = id;
   canHardware->ClearUserMessages();
}

void CanSdo::InitiateSDOTransfer(uint8_t req, uint8_t nodeId, uint16_t index, uint8_t subIndex, uint32_t data)
{
   uint32_t d[2];

#ifdef C2000
   d[0] = (uint32_t)(req & 0xFFUL) |
          ((uint32_t)(index & 0xFFUL) << 8) |
          ((uint32_t)((index >> 8) & 0xFFUL) << 16) |
          ((uint32_t)(subIndex & 0xFFUL) << 24);
   d[1] = data;
#else
   SdoFrame *sdo = (SdoFrame*)d;
   sdo->cmd = req;
   sdo->index = index;
   sdo->subIndex = subIndex;
   sdo->data = data;
#endif

   if (nodeId != remoteNodeId)
   {
      remoteNodeId = nodeId;
      canHardware->ClearUserMessages();
   }

   sdoReplyValid = false;
   canHardware->Send(SDO_REQ_ID_BASE + remoteNodeId, d);
}

void CanSdo::ProcessSDO(uint32_t data[2])
{
   #ifdef C2000
      uint16_t sdo_cmd      = (uint16_t)(data[0] & 0xFFUL);
      uint16_t sdo_index    = (uint16_t)(((data[0] >> 8) & 0xFFUL) | (((data[0] >> 16) & 0xFFUL) << 8));
      uint16_t sdo_subIndex = (uint16_t)((data[0] >> 24) & 0xFFUL);
      uint32_t sdo_data     = data[1];

      SdoFrame sdoLocal;
      sdoLocal.cmd      = (uint8_t)sdo_cmd;
      sdoLocal.index    = sdo_index;
      sdoLocal.subIndex = (uint8_t)sdo_subIndex;
      sdoLocal.data     = sdo_data;
      SdoFrame *sdo = &sdoLocal;
   #else
      SdoFrame sdoLocal;
      SdoFrame *sdo = &sdoLocal;
      *sdo = *(SdoFrame*)data;
   #endif

   if ((sdo->cmd & SDO_REQUEST_SEGMENT) == SDO_REQUEST_SEGMENT)
   {
      const int bytesPerMessage = 7;
      sdo->cmd = sdo->cmd & SDO_TOGGLE_BIT;

   #ifdef C2000
      uint8_t segBytes[7] = {0};
      int i = 0;

      for (; i < bytesPerMessage && !PrintBufEmpty(printByteIn, printByteOut); i++)
         segBytes[i] = PrintBufDequeue(printByteOut);

      // If no data available yet, don't respond - let oic retry
      //if (i == 0) return;

      if (PrintBufEmpty(printByteIn, printByteOut))
      {
         sdo->cmd |= SDO_SIZE_SPECIFIED;
         sdo->cmd |= (uint8_t)((bytesPerMessage - i) << 1);
      }

      data[0] = (uint32_t)(sdo->cmd & 0xFFUL) |
               ((uint32_t)segBytes[0] << 8) |
               ((uint32_t)segBytes[1] << 16) |
               ((uint32_t)segBytes[2] << 24);

      data[1] = (uint32_t)segBytes[3] |
               ((uint32_t)segBytes[4] << 8) |
               ((uint32_t)segBytes[5] << 16) |
               ((uint32_t)segBytes[6] << 24);

      canHardware->Send(0x580 + nodeId, data);
      return;
   #else
      uint8_t *bytes = (uint8_t*)data;
      int i = 1;
      sdo->cmd = sdo->cmd & SDO_TOGGLE_BIT;

      for (; i <= bytesPerMessage && !PrintBufEmpty(printByteIn, printByteOut); i++)
         bytes[i] = PrintBufDequeue(printByteOut);

      if (PrintBufEmpty(printByteIn, printByteOut))
      {
         sdo->cmd |= SDO_SIZE_SPECIFIED;
         sdo->cmd |= (bytesPerMessage - i + 1) << 1;
      }
   #endif
   }
   else if (sdo->index == SDO_INDEX_PARAMS || (sdo->index & 0xFF00) == SDO_INDEX_PARAM_UID)
   {
      Param::PARAM_NUM paramIdx = (Param::PARAM_NUM)sdo->subIndex;

      if ((sdo->index & 0xFF00) == SDO_INDEX_PARAM_UID)
         paramIdx = Param::NumFromId(sdo->subIndex + ((sdo->index & 0xFF) << 8));

      if (paramIdx < Param::PARAM_LAST)
      {
         if (sdo->cmd == SDO_WRITE)
         {
            if (Param::Set(paramIdx, sdo->data) == 0)
            {
               sdo->cmd = SDO_WRITE_REPLY;
            }
            else
            {
               sdo->cmd = SDO_ABORT;
               sdo->data = SDO_ERR_RANGE;
            }
         }
         else if (sdo->cmd == SDO_READ)
         {
            sdo->data = Param::Get(paramIdx);
            sdo->cmd = SDO_READ_REPLY;
         }
      }
      else
      {
         sdo->cmd = SDO_ABORT;
         sdo->data = SDO_ERR_INVIDX;
      }
   }
   else if (0 != canMap && sdo->index == SDO_INDEX_MAP_TX)
   {
      AddCanMap(sdo, false);
   }
   else if (0 != canMap && sdo->index == SDO_INDEX_MAP_RX)
   {
      AddCanMap(sdo, true);
   }
   else if (0 != canMap && (sdo->index & 0xFF00) == SDO_INDEX_MAP_RD)
   {
      ReadOrDeleteCanMap(sdo);
   }
   else if (sdo->index == SDO_INDEX_ERROR_NUM)
   {
      if (sdo->cmd == SDO_READ)
      {
         sdo->data = ErrorMessage::GetErrorNum(sdo->subIndex);
         sdo->cmd = SDO_READ_REPLY;
      }
      else
      {
         sdo->cmd = SDO_ABORT;
         sdo->data = SDO_ERR_INVIDX;
      }
   }
   else if (sdo->index == SDO_INDEX_ERROR_TIME)
   {
      if (sdo->cmd == SDO_READ)
      {
         sdo->data = ErrorMessage::GetErrorTime(sdo->subIndex);
         sdo->cmd = SDO_READ_REPLY;
      }
      else
      {
         sdo->cmd = SDO_ABORT;
         sdo->data = SDO_ERR_INVIDX;
      }
   }
   else
   {
      if (!ProcessSpecialSDOObjects(sdo))
         return;
   }

   #ifdef C2000
      data[0] = (uint32_t)(sdo->cmd & 0xFFUL) |
               ((uint32_t)(sdo->index & 0xFFUL) << 8) |
               ((uint32_t)((sdo->index >> 8) & 0xFFUL) << 16) |
               ((uint32_t)(sdo->subIndex & 0xFFUL) << 24);
      data[1] = sdo->data;
   #else
      *(SdoFrame*)data = *sdo;
   #endif

   canHardware->Send(0x580 + nodeId, data);
}

void CanSdo::TriggerTimeout(int callingFrequency)
{
   if (printTimeout > 0)
      printTimeout -= callingFrequency;

   if (printTimeout < 0)
      printTimeout = 0;
}

void CanSdo::PutChar(char c)
{
   if (printTimeout == 0)
      return;

   printTimeout = PRINT_TIMEOUT;

   if (PrintBufFull(printByteIn, printByteOut))
   {
      printOverflow = true;
      return;
   }

   PrintBufEnqueue(printByteIn, (uint8_t)c);
}

void CanSdo::SendSdoReply(SdoFrame* sdoFrame)
{
   uint32_t buf[2];

#ifdef C2000
   buf[0] = (uint32_t)(sdoFrame->cmd & 0xFFUL) |
            ((uint32_t)(sdoFrame->index & 0xFFUL) << 8) |
            ((uint32_t)((sdoFrame->index >> 8) & 0xFFUL) << 16) |
            ((uint32_t)(sdoFrame->subIndex & 0xFFUL) << 24);
   buf[1] = sdoFrame->data;
#else
   const uint8_t *src = (const uint8_t*)sdoFrame;
   uint8_t *dst = (uint8_t*)buf;
   for (int i = 0; i < (int)sizeof(SdoFrame); i++)
      dst[i] = src[i];
#endif

   canHardware->Send(0x580 + nodeId, buf);
   pendingUserSpaceSdo = false;
}

bool CanSdo::ProcessSpecialSDOObjects(SdoFrame* sdo)
{
   if (sdo->index == SDO_INDEX_STRINGS)
   {
      if (sdo->cmd == SDO_READ)
      {
         sdo->data = 65535; // keep original behavior for OIC compatibility
         sdo->cmd = SDO_RESPONSE_UPLOAD | SDO_SIZE_SPECIFIED;
         printTimeout = PRINT_TIMEOUT;
         printByteIn = 0;
         printByteOut = 0; // virtual offset, fixed for C2000
         printOverflow = false;
         printRequest = sdo->subIndex;
         if (printCallback) printCallback(this, printRequest);
         return true;
      }
   }
   else
   {
      pendingUserSpaceSdo = true;
      pendingUserSpaceSdoFrame.cmd = sdo->cmd;
      pendingUserSpaceSdoFrame.index = sdo->index;
      pendingUserSpaceSdoFrame.subIndex = sdo->subIndex;
      pendingUserSpaceSdoFrame.data = sdo->data;
   }
   return false;
}

void CanSdo::ReadOrDeleteCanMap(SdoFrame* sdo)
{
   bool rx = (sdo->index & 0x80) != 0;
   uint32_t canId;
   uint16_t itemIdx = (sdo->subIndex > 0) ? (sdo->subIndex - 1) / 2 : 0;
   const CanMap::CANPOS* canPos = canMap->GetMap(rx, sdo->index & 0x3f, itemIdx, canId);

   if (sdo->cmd == SDO_READ)
   {
      if (canPos != 0)
      {
         uint16_t id = Param::GetAttrib((Param::PARAM_NUM)canPos->mapParam)->id;

         if (sdo->subIndex == 0)
            sdo->data = canId;
         else if (sdo->subIndex & 1)
            sdo->data = id | ((uint32_t)canPos->offsetBits << 16) | ((uint32_t)canPos->numBits << 24);
         else
            sdo->data = (uint32_t)(((int32_t)(canPos->gain * 1000)) & 0xFFFFFF) | ((uint32_t)canPos->offset << 24);

         sdo->cmd = SDO_READ_REPLY;
      }
      else
      {
         sdo->cmd = SDO_ABORT;
         sdo->data = SDO_ERR_INVIDX;
      }
   }
   else if (sdo->cmd == SDO_WRITE && canPos != 0 && sdo->data == 0)
   {
      canMap->Remove(rx, sdo->index & 0x3f, itemIdx);
      sdo->cmd = SDO_WRITE_REPLY;
   }
   else
   {
      sdo->cmd = SDO_ABORT;
      sdo->data = SDO_ERR_INVIDX;
   }
}

void CanSdo::AddCanMap(SdoFrame* sdo, bool rx)
{
   if (sdo->cmd == SDO_WRITE)
   {
      int result = -1;

      if (sdo->subIndex == 0)
      {
         if (sdo->data < 0x20000000 || (sdo->data & ~CAN_FORCE_EXTENDED) < 0x800)
         {
            mapId = sdo->data;
            result = 0;
         }
         else
         {
            mapId = 0xFFFFFFFF;
         }
      }
      else if (mapId != 0xFFFFFFFF && sdo->subIndex == 1)
      {
         mapInfo.mapParam = Param::NumFromId(sdo->data & 0xFFFF);
         mapInfo.offsetBits = (sdo->data >> 16) & 0x3F;
         mapInfo.numBits = ((int32_t)sdo->data >> 24);
         result = mapInfo.mapParam < Param::PARAM_LAST ? 0 : -1;
      }
      else if (mapInfo.numBits != 0 && sdo->subIndex == 2)
      {
         int32_t gainFixedPoint = (sdo->data & 0xFFFFFF) << (32 - 24);
         gainFixedPoint >>= (32 - 24);
         mapInfo.gain = gainFixedPoint / 1000.0f;
         mapInfo.offset = sdo->data >> 24;

         if (rx)
            result = canMap->AddRecv((Param::PARAM_NUM)mapInfo.mapParam, mapId, mapInfo.offsetBits, mapInfo.numBits, mapInfo.gain, mapInfo.offset);
         else
            result = canMap->AddSend((Param::PARAM_NUM)mapInfo.mapParam, mapId, mapInfo.offsetBits, mapInfo.numBits, mapInfo.gain, mapInfo.offset);

         mapInfo.numBits = 0;
         mapId = 0xFFFFFFFF;
      }

      if (result >= 0)
      {
         sdo->cmd = SDO_WRITE_REPLY;
      }
      else
      {
         sdo->cmd = SDO_ABORT;
         sdo->data = SDO_ERR_INVIDX;
      }
   }
}
