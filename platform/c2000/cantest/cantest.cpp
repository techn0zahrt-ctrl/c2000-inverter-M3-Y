#include "driverlib.h"
#include "device.h"
#include <stdio.h>
#include <stdbool.h>

#define PRINTF(...) do { DINT; printf(__VA_ARGS__); EINT; } while(0)

#define CAN_BASE      CANA_BASE
#define TX_OBJ_ID     1U
#define RX_OBJ_ID     2U

static uint16_t txBuf[8] = {0};
static volatile uint16_t rxBuf[8] = {0};

static volatile uint8_t  rxBytes[8] = {0};
static volatile uint32_t rxCount = 0;
static volatile uint16_t rxPending = 0;
static volatile uint32_t canStatus = 0;
static volatile uint16_t canStatusPending = 0;

static void canPack8(uint16_t *buf, const uint8_t *d)
{
    buf[0] = 0;
    buf[1] = ((uint16_t)d[0]) | (((uint16_t)d[1]) << 8);
    buf[2] = 0;
    buf[3] = ((uint16_t)d[2]) | (((uint16_t)d[3]) << 8);
    buf[4] = 0;
    buf[5] = ((uint16_t)d[4]) | (((uint16_t)d[5]) << 8);
    buf[6] = 0;
    buf[7] = ((uint16_t)d[6]) | (((uint16_t)d[7]) << 8);
}

static void canUnpack8(const volatile uint16_t *buf, volatile uint8_t *d)
{
    d[0] = (uint8_t)(buf[1] & 0xFF);
    d[1] = (uint8_t)((buf[1] >> 8) & 0xFF);
    d[2] = (uint8_t)(buf[3] & 0xFF);
    d[3] = (uint8_t)((buf[3] >> 8) & 0xFF);
    d[4] = (uint8_t)(buf[5] & 0xFF);
    d[5] = (uint8_t)((buf[5] >> 8) & 0xFF);
    d[6] = (uint8_t)(buf[7] & 0xFF);
    d[7] = (uint8_t)((buf[7] >> 8) & 0xFF);
}

__interrupt void canISR(void)
{
    uint32_t cause = CAN_getInterruptCause(CAN_BASE);

    if(cause == RX_OBJ_ID)
    {
        if(CAN_readMessage(CAN_BASE, RX_OBJ_ID, (uint16_t *)rxBuf))
        {
            canUnpack8(rxBuf, rxBytes);
            rxCount++;
            rxPending = 1;
        }

        //
        // Explicitly clear this mailbox interrupt source
        //
        CAN_clearInterruptStatus(CAN_BASE, RX_OBJ_ID);
    }
    else if(cause == CAN_INT_INT0ID_STATUS)
    {
        canStatus = CAN_getStatus(CAN_BASE);
        canStatusPending = 1;

        //
        // Clear CAN status interrupt source
        //
        CAN_clearInterruptStatus(CAN_BASE, CAN_INT_INT0ID_STATUS);
    }

    //
    // Clear CANINT0 global flag
    //
    CAN_clearGlobalInterruptStatus(CAN_BASE, CAN_GLOBAL_INT_CANINT0);

    //
    // Acknowledge PIE group 9
    //
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

void main(void)
{
    uint8_t txBytes[8] = {0x4A, 0x5B, 0x6C, 0x7D, 0x8E, 0x9F, 0xB0, 0xC1};

    Device_init();
    Device_initGPIO();

    Interrupt_initModule();
    Interrupt_initVectorTable();

    GPIO_setPinConfig(DEVICE_GPIO_CFG_CANRXA);
    GPIO_setPinConfig(DEVICE_GPIO_CFG_CANTXA);
    GPIO_setDirectionMode(5U, GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(4U, GPIO_DIR_MODE_OUT);
    GPIO_setQualificationMode(5U, GPIO_QUAL_ASYNC);

    CAN_initModule(CAN_BASE);
    CAN_setBitRate(CAN_BASE, 200000000UL, 500000UL, 16U);

    CAN_setupMessageObject(CAN_BASE,
                           TX_OBJ_ID,
                           0x100,
                           CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_TX,
                           0,
                           CAN_MSG_OBJ_NO_FLAGS,
                           8);

    CAN_setupMessageObject(CAN_BASE,
                           RX_OBJ_ID,
                           0,
                           CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_RX,
                           0,
                           CAN_MSG_OBJ_USE_ID_FILTER | CAN_MSG_OBJ_RX_INT_ENABLE,
                           8);

    Interrupt_register(INT_CANA0, &canISR);
    Interrupt_enable(INT_CANA0);

    CAN_enableInterrupt(CAN_BASE, CAN_INT_IE0 | CAN_INT_ERROR | CAN_INT_STATUS);
    CAN_enableGlobalInterrupt(CAN_BASE, CAN_GLOBAL_INT_CANINT0);

    CAN_startModule(CAN_BASE);

    EINT;
    ERTM;

    PRINTF("CAN interrupt example start\r\n");
    EALLOW;
    uint32_t gpamux1 = HWREG(0x7C00U + 0x6U);
    uint32_t gpadir = HWREG(0x7C00U + 0xCU);
    uint32_t gpagmux1 = HWREG(0x7C00U + 0x20U); // GPAGMUX1
    EDIS;
    PRINTF("GPAMUX1=0x%x GPADIR=0x%x\n", (uint16_t)gpamux1, (uint16_t)gpadir);
    PRINTF("GPAGMUX1=0x%x\n", (uint16_t)gpagmux1);

    while(1)
    {
        if(rxPending)
        {
            rxPending = 0;
                PRINTF("RX[%lu]: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   rxCount,
                   rxBytes[0], rxBytes[1], rxBytes[2], rxBytes[3],
                   rxBytes[4], rxBytes[5], rxBytes[6], rxBytes[7]);
        }

        if(canStatusPending)
        {
            canStatusPending = 0;
            PRINTF("CAN STATUS: 0x%04lX\r\n", canStatus);
        }

        canPack8(txBuf, txBytes);
        CAN_sendMessage(CAN_BASE, TX_OBJ_ID, 8, txBuf);

        PRINTF("TX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               txBytes[0], txBytes[1], txBytes[2], txBytes[3],
               txBytes[4], txBytes[5], txBytes[6], txBytes[7]);

        for(int i = 0; i < 8; i++)
        {
            //if (txBytes[i] == 0xFF) txBytes[i] = 0;
            //else txBytes[i]++;
            txBytes[i] = 0;
        }
        txBytes[0] = 2;

        DEVICE_DELAY_US(1000000);
    }
}
