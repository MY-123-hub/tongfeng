#ifndef MASTER_CALENDAR_H
#define MASTER_CALENDAR_H

#include <stdint.h>

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} MasterDateTime;

uint8_t MasterCalendar_IsValid(const MasterDateTime *time);
int8_t MasterCalendar_Compare(const MasterDateTime *left, const MasterDateTime *right);
void MasterCalendar_AddMinutes(MasterDateTime *time, uint32_t minutes);
void MasterCalendar_ToWords(const MasterDateTime *time, uint16_t words[5]);
void MasterCalendar_FromWords(MasterDateTime *time, const uint16_t words[5]);

#endif /* MASTER_CALENDAR_H */
