#include "printf.h"
#include "params.h"
#include "canmap.h"
#include "cansdo.h"
#include "my_fp.h"
#include <string.h>

void PrintParamsJson(IPutChar* term, CanMap* canMap)
{
   CanSdo* sdo = (CanSdo*)term;
   const Param::Attributes *pAtr;

   static bool active = false;
   static uint32_t idx = 0;
   static char comma = ' ';
   static int stage = 0;

   static char outBuf[512];
   static uint16_t outLen = 0;
   static uint16_t outPos = 0;
   static bool chunkReady = false;

#define BUFFER_LOW() (sdo->GetPrintFree() < 16 || sdo->GetPrintOverflow())
//#define BUFFER_LOW() (sdo->GetPrintFree() == 0)
   if (!active)
   {
      active = true;
      idx = 0;
      comma = ' ';
      stage = 0;
      outLen = 0;
      outPos = 0;
      chunkReady = false;
      sdo->ResetPrintOverflow();
   }

   while (1)
   {
      if (chunkReady)
      {
         while (outPos < outLen)
         {
            if (BUFFER_LOW())
               return;

            term->PutChar(outBuf[outPos]);
            outPos++;
         }

         chunkReady = false;
         outLen = 0;
         outPos = 0;

         if (stage == 1)
         {
            idx++;
         }
         else if (stage == 2)
         {
            stage = 3;
         }
         else if (stage == 3)
         {
            stage = 4;
         }

         continue;
      }

      switch (stage)
      {
         case 0:
            strcpy(outBuf, "{");
            outLen = 1;
            outPos = 0;
            chunkReady = true;
            stage = 1;
            break;

         case 1:
         {
            while (idx < Param::PARAM_LAST)
            {
               uint32_t canId;
               uint8_t canStart;
               int8_t canLength, offset;
               bool isRx;
               float canGain;
               int n = 0;

               pAtr = Param::GetAttrib((Param::PARAM_NUM)idx);

               if ((Param::GetFlag((Param::PARAM_NUM)idx) & Param::FLAG_HIDDEN) != 0)
               {
                  idx++;
                  continue;
               }

               n += sprintf(outBuf + n,
                            "%c\r\n   \"%s\": {\"unit\":\"%s\",\"id\":%d,\"value\":%f,",
                            comma,
                            pAtr->name,
                            pAtr->unit,
                            (uint16_t)pAtr->id,
                            (float)((float)Param::Get((Param::PARAM_NUM)idx) / 32.0f));

               if (canMap->FindMap((Param::PARAM_NUM)idx, canId, canStart, canLength, canGain, offset, isRx))
               {
                  n += sprintf(outBuf + n,
                               "\"canid\":%d,\"canoffset\":%d,\"canlength\":%d,\"cangain\":%f,\"canadd\":%d,\"isrx\":%s,",
                               (uint16_t)canId,
                               (uint16_t)canStart,
                               (int16_t)canLength,
                               (float)canGain,
                               (int16_t)offset,
                               isRx ? "true" : "false");
               }

               if (Param::GetType((Param::PARAM_NUM)idx) == Param::TYPE_PARAM ||
                   Param::GetType((Param::PARAM_NUM)idx) == Param::TYPE_TESTPARAM)
               {
                  n += sprintf(outBuf + n,
                               "\"isparam\":true,\"minimum\":%f,\"maximum\":%f,\"default\":%f,\"category\":\"%s\",\"i\":%d}",
                               (float)FP_TOFLOAT(pAtr->min),
                               (float)FP_TOFLOAT(pAtr->max),
                               (float)FP_TOFLOAT(pAtr->def),
                               pAtr->category,
                               (uint16_t)idx);
               }
               else
               {
                  n += sprintf(outBuf + n, "\"isparam\":false}");
               }

               if (n < 0)
                  n = 0;
               if (n > (int)sizeof(outBuf))
                  n = sizeof(outBuf);

               outLen = (uint16_t)n;
               outPos = 0;
               chunkReady = true;
               comma = ',';
               break;
            }

            if (!chunkReady)
            {
               stage = 2;
            }
            break;
         }

         case 2:
         {
            int n = sprintf(outBuf,
                            ",\r\n   \"serial\": {\"unit\":\"\",\"value\":\"00000000\",\"isparam\":false}");
            if (n < 0)
               n = 0;
            if (n > (int)sizeof(outBuf))
               n = sizeof(outBuf);

            outLen = (uint16_t)n;
            outPos = 0;
            chunkReady = true;
            break;
         }

         case 3:
            strcpy(outBuf, "\r\n}\r\n");
            outLen = 4;
            outPos = 0;
            chunkReady = true;
            break;

         case 4:
            active = false;
            sdo->ClearPrintRequest();
            return;
      }
   }

#undef BUFFER_LOW
}
