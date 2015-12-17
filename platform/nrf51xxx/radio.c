/*
 * Copyright (c) 2015 Eric Holland
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <platform/nrf51.h>
#include <lib/ble.h>
#include <dev/ble_radio.h>
#include <platform/nrf51_radio.h>


#define PCNF0_ADV_PDU       6 << RADIO_PCNF0_LFLEN_Pos | \
                            1 << RADIO_PCNF0_S0LEN_Pos | \
                            2 << RADIO_PCNF0_S1LEN_Pos
#define PCNF0_DATA_PDU      5 << RADIO_PCNF0_LFLEN_Pos | \
                            1 << RADIO_PCNF0_S0LEN_Pos | \
                            3 << RADIO_PCNF0_S1LEN_Pos


static nrf_packet_buffer_t _packetbuffer;

#define NRF_RADIO_CAST(x)   ((NRF_RADIO_Type *)(x->radio_handle))

/*
    Initialize the radio to ble mode, place in idle.
    This will perform necessary checks to ensure configuration
    is viable to ramp up for transmission or reception.

*/
void ble_radio_initialize(ble_t *ble_p) {

    NRF_CLOCK->TASKS_HFCLKSTART = 1;    //Start HF xtal oscillator (required for radio)

    
    NRF_RADIO_CAST(ble_p)->POWER =1;

    if ((NRF_FICR->OVERRIDEEN & FICR_OVERRIDEEN_BLE_1MBIT_Msk) == \
                    (FICR_OVERRIDEEN_BLE_1MBIT_Override << FICR_OVERRIDEEN_BLE_1MBIT_Pos))
    {
        NRF_RADIO_CAST(ble_p)->OVERRIDE0 = NRF_FICR->BLE_1MBIT[0];
        NRF_RADIO_CAST(ble_p)->OVERRIDE1 = NRF_FICR->BLE_1MBIT[1];
        NRF_RADIO_CAST(ble_p)->OVERRIDE2 = NRF_FICR->BLE_1MBIT[2];
        NRF_RADIO_CAST(ble_p)->OVERRIDE3 = NRF_FICR->BLE_1MBIT[3];
        NRF_RADIO_CAST(ble_p)->OVERRIDE4 = NRF_FICR->BLE_1MBIT[4] | \
                (RADIO_OVERRIDE4_ENABLE_Enabled << RADIO_OVERRIDE4_ENABLE_Pos);
    }

    NRF_RADIO_CAST(ble_p)->CRCCNF =     RADIO_CRCCNF_LEN_Three      << RADIO_CRCCNF_LEN_Pos  | \
                            RADIO_CRCCNF_SKIPADDR_Skip  << RADIO_CRCCNF_SKIPADDR_Pos;

    NRF_RADIO_CAST(ble_p)->CRCPOLY      =   BLE_CRC_POLYNOMIAL;
    NRF_RADIO_CAST(ble_p)->TXPOWER      =   RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos;
    NRF_RADIO_CAST(ble_p)->MODE         =   RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos;
    NRF_RADIO_CAST(ble_p)->PCNF0        =   255 << RADIO_PCNF1_MAXLEN_Pos   | \
                                3   << RADIO_PCNF1_BALEN_Pos | \
                                RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos | \
                                RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos;
    NRF_RADIO_CAST(ble_p)->TXADDRESS    =   0;
}


/*
    Returns hw addr(mac) and type.  platforms not suporting feature return -1

*/
int32_t ble_get_hw_addr(uint8_t * addr_p, ble_addr_type_t * addr_type_p) {

    addr_p[0] = ( NRF_FICR->DEVICEADDR[0] >> 0  ) & 0xff;
    addr_p[1] = ( NRF_FICR->DEVICEADDR[0] >> 8  ) & 0xff;
    addr_p[2] = ( NRF_FICR->DEVICEADDR[0] >> 16 ) & 0xff;
    addr_p[3] = ( NRF_FICR->DEVICEADDR[0] >> 24 ) & 0xff;
    addr_p[4] = ( NRF_FICR->DEVICEADDR[1] >> 0  ) & 0xff;
    addr_p[5] = ( NRF_FICR->DEVICEADDR[1] >> 8  ) & 0xff;

    *addr_type_p = HW_ADDR_TYPE_RANDOM;

    return 0;
}



void ble_packet_initialize(ble_t *ble_p) {

        if ( ble_p->packet_type == ADV_CHANNEL_PDU) {
            NRF_RADIO_CAST(ble_p)->PCNF0 = PCNF0_ADV_PDU;
        } else {
            NRF_RADIO_CAST(ble_p)->PCNF0 = PCNF0_DATA_PDU;
        }
        NRF_RADIO_CAST(ble_p)->BASE0        =   ble_p->access_address & 0x00ffffff;
        NRF_RADIO_CAST(ble_p)->PREFIX0      =   (ble_p->access_address >> 24) & 0xff;
        NRF_RADIO_CAST(ble_p)->CRCINIT      =   ble_p->crc_init;
        NRF_RADIO_CAST(ble_p)->DATAWHITEIV  =   ble_p->channel;


}



