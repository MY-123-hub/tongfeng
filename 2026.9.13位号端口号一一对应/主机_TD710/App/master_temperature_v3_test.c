#include "master_temperature.h"

#include <stdio.h>
#include <string.h>

#define TEST_POINT_COUNT        (36U)
#define TEST_PAYLOAD_SIZE       (112U)
#define TEST_INVALID_TEMPERATURE ((int16_t)0x8000)

static void WriteU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void SetPoint(uint8_t *payload, uint8_t record_index,
                     uint8_t position, int16_t temperature_x10)
{
    uint16_t offset = (uint16_t)record_index * 3U;

    payload[offset] = position;
    WriteU16(&payload[offset + 1U], (uint16_t)temperature_x10);
}

static int Test_ReordersPortByPhysicalPosition(void)
{
    static const uint8_t received_order[6] = {3U, 1U, 6U, 2U, 5U, 4U};
    static const int16_t received_temperature[6] = {303, 101, 606, 202, 505, 404};
    uint8_t payload[TEST_PAYLOAD_SIZE];
    int16_t temperatures[TEST_POINT_COUNT];
    uint8_t positions[TEST_POINT_COUNT];
    int16_t bme_temperature;
    uint8_t index;

    memset(payload, 0, sizeof(payload));
    for (index = 0U; index < 6U; index++)
    {
        SetPoint(payload, index, received_order[index], received_temperature[index]);
    }
    for (index = 6U; index < TEST_POINT_COUNT; index++)
    {
        SetPoint(payload, index, 0U, TEST_INVALID_TEMPERATURE);
    }
    WriteU16(&payload[108], 251);
    WriteU16(&payload[110], (uint16_t)TEST_INVALID_TEMPERATURE);

    if (MasterTemperature_DecodeSlaveV3(payload, sizeof(payload), temperatures,
                                        positions, &bme_temperature) == 0U)
    {
        return 1;
    }
    for (index = 0U; index < 6U; index++)
    {
        if ((positions[index] != (uint8_t)(index + 1U)) ||
            (temperatures[index] != (int16_t)((index + 1U) * 101)))
        {
            return 1;
        }
    }
    return (bme_temperature == 251) ? 0 : 1;
}

static int Test_RejectsDuplicatePositionOnOnePort(void)
{
    uint8_t payload[TEST_PAYLOAD_SIZE];
    int16_t temperatures[TEST_POINT_COUNT];
    uint8_t positions[TEST_POINT_COUNT];
    int16_t bme_temperature;
    uint8_t index;

    memset(payload, 0, sizeof(payload));
    for (index = 0U; index < TEST_POINT_COUNT; index++)
    {
        SetPoint(payload, index, 0U, TEST_INVALID_TEMPERATURE);
    }
    SetPoint(payload, 0U, 1U, 200);
    SetPoint(payload, 1U, 1U, 210);

    return (MasterTemperature_DecodeSlaveV3(payload, sizeof(payload), temperatures,
                                            positions, &bme_temperature) == 0U) ? 0 : 1;
}

int main(void)
{
    int failed = 0;

    failed += Test_ReordersPortByPhysicalPosition();
    failed += Test_RejectsDuplicatePositionOnOnePort();
    if (failed != 0)
    {
        printf("master_temperature_v3_test: %d failure(s)\n", failed);
    }
    return failed;
}
