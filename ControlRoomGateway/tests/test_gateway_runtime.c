#include "gateway_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t sent_port[8];
static uint8_t sent_frame[8][109];
static uint16_t sent_length[8];
static uint8_t sent_count;

static uint16_t crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;
    for (i = 0U; i < length; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
    }
    return crc;
}

static uint16_t frame(uint8_t *out, uint8_t type, uint8_t sr, uint8_t sg,
                      uint8_t dr, uint8_t dg, uint16_t flow, const uint8_t *payload, uint8_t len)
{
    uint16_t crc;
    out[0]=0xAA; out[1]=0x55; out[2]=1; out[3]=type; out[4]=sr; out[5]=sg; out[6]=dr; out[7]=dg;
    out[8]=(uint8_t)flow; out[9]=(uint8_t)(flow>>8); out[10]=len;
    if (len) memcpy(&out[11], payload, len);
    crc=crc16(&out[2], (uint16_t)(9U+len)); out[11+len]=(uint8_t)crc; out[12+len]=(uint8_t)(crc>>8);
    return (uint16_t)(13U+len);
}

static void send_cb(GatewayOutputPort port, const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx; sent_port[sent_count]=(uint8_t)port; sent_length[sent_count]=len; memcpy(sent_frame[sent_count++],data,len);
}

static void feed_pc(const uint8_t *data, uint16_t len) { uint16_t i; for(i=0;i<len;i++) GatewayRuntime_PushPcByteFromIsr(data[i]); }
static void feed_lora(const uint8_t *data, uint16_t len) { uint16_t i; for(i=0;i<len;i++) GatewayRuntime_PushLoRaByteFromIsr(data[i]); }

int main(void)
{
    uint8_t in[109]; uint16_t len; uint8_t freq[2]={0x88,0x13}; uint8_t result[7]={0,0,0,0,0,0,0};
    GatewayRuntime_Init(send_cb, 0);
    GatewayRuntime_Process(0);
    assert(sent_count==1 && sent_port[0]==GATEWAY_OUTPUT_LORA && sent_frame[0][3]==1 && sent_frame[0][7]==1);
    len=frame(in,2,2,1,1,0,0x8000,0,0); feed_lora(in,len); GatewayRuntime_Process(1);
    assert(sent_count==2 && sent_port[1]==GATEWAY_OUTPUT_PC && sent_frame[1][3]==2);
    len=frame(in,0x10,1,0,2,2,100,freq,2); feed_pc(in,len); GatewayRuntime_Process(2);
    assert(sent_count==3 && sent_port[2]==GATEWAY_OUTPUT_LORA && sent_frame[2][3]==0x10 && sent_frame[2][7]==2);
    len=frame(in,0x21,2,2,1,0,100,result,7); feed_lora(in,len); GatewayRuntime_Process(3);
    assert(sent_count==4 && sent_port[3]==GATEWAY_OUTPUT_PC && sent_frame[3][3]==0x21);
    return 0;
}
