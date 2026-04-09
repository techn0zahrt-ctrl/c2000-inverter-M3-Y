#include "printf.h"
#include "params.h"
#include "canmap.h"
#include "cansdo.h"
#include "my_fp.h"
#include <string.h>

#include <stdint.h>

/*
 * Convert float to string with fixed number of decimal places.
 * 
 * @param buf       Destination buffer (must be large enough)
 * @param val       Float value to convert
 * @param decimals  Number of digits after decimal point (0..6 recommended)
 * @return          Pointer to the start of the string in buf
 */
char* ftoa(char* buf, float val, int decimals)
{
    char* p = buf;
    //uint32_t uval;
    int32_t ival;

    // Handle negative sign
    if (val < 0.0f) {
        *p++ = '-';
        val = -val;
    }

    // Separate integer and fractional parts
    ival = (int32_t)val;                    // integer part
    float frac = val - (float)ival;         // fractional part

    // Convert integer part (reuse simple int32 logic)
    if (ival == 0) {
        *p++ = '0';
    } else {
        char* start = p;
        while (ival > 0) {
            *p++ = (char)('0' + (ival % 10));
            ival /= 10;
        }
        // Reverse the digits
        char* end = p - 1;
        while (start < end) {
            char tmp = *start;
            *start++ = *end;
            *end-- = tmp;
        }
    }

    // Add decimal point and fractional digits
    if (decimals > 0) {
        *p++ = '.';

        // Multiply fractional part by 10^decimals and round
        for (int i = 0; i < decimals; i++) {
            frac *= 10.0f;
        }
        uint32_t fpart = (uint32_t)(frac + 0.5f);   // round to nearest

        // Print decimals (with leading zeros if needed)
        for (int i = decimals - 1; i >= 0; i--) {
            p[i] = (char)('0' + (fpart % 10));
            fpart /= 10;
        }
        p += decimals;
    }

    *p = '\0';   // null-terminate
    return buf;
}

void PrintParamsJson(IPutChar* term, CanMap* canMap)
{
   CanSdo* sdo = (CanSdo*)term;
   const Param::Attributes *pAtr;

   static bool active = false;
   static int32_t idx = 0;
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
      idx = -1;
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

               char val_str[16];
               ftoa(val_str, (float)Param::Get((Param::PARAM_NUM)idx) / 32.0f, 3);
               n += sprintf(outBuf + n,
                            "%c\r\n   \"%s\": {\"unit\":\"%s\",\"id\":%d,\"value\":%s,",
                            comma,
                            pAtr->name,
                            pAtr->unit,
                            (uint16_t)pAtr->id,
                            val_str);
                           //(float)((float)Param::Get((Param::PARAM_NUM)idx) / 32.0f));

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
                  char min_str[16], max_str[16], def_str[16];

                  ftoa(min_str, (float)pAtr->min / 32.0f, 3);   // adjust decimals as needed
                  ftoa(max_str, (float)pAtr->max / 32.0f, 3);
                  ftoa(def_str, (float)pAtr->def / 32.0f, 3);

                  sprintf(outBuf + n, "\"minimum\":%s,\"maximum\":%s,\"default\":%s",
                  min_str, max_str, def_str);
                  n += sprintf(outBuf + n,
                               "\"isparam\":true,\"minimum\":%s,\"maximum\":%s,\"default\":%s,\"category\":\"%s\",\"i\":%d}",
                               min_str, max_str, def_str,
                               pAtr->category,
                               (uint16_t)idx);

                  /*
                  int32_t fmin = pAtr->min/32;
                  int32_t fmax = pAtr->max/32;
                  int32_t fdef = pAtr->def/32;

                  n += sprintf(outBuf + n,
                               "\"isparam\":true,\"minimum\":%d%d,\"maximum\":%d%d,\"default\":%d%d,\"category\":\"%s\",\"i\":%d}",
                               (uint16_t)(fmin >> 16),(uint16_t)fmin,
                               (uint16_t)(fmin >> 16),(uint16_t)fmax,
                               (uint16_t)(fmin >> 16),(uint16_t)fdef,
                               pAtr->category,
                               (uint16_t)idx);
                  */
                  //    (uint16_t)(canLastStatus >> 16),
                  //    (uint16_t)canLastStatus);

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
