#include "master_calendar.h"

static uint8_t MasterCalendar_IsLeapYear(uint16_t year)
{
    return (((year % 4U) == 0U) && (((year % 100U) != 0U) ||
            ((year % 400U) == 0U))) ? 1U : 0U;
}

static uint8_t MasterCalendar_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                   31U, 31U, 30U, 31U, 30U, 31U};

    if ((month == 0U) || (month > 12U))
    {
        return 0U;
    }
    if ((month == 2U) && (MasterCalendar_IsLeapYear(year) != 0U))
    {
        return 29U;
    }
    return days[month - 1U];
}

uint8_t MasterCalendar_IsValid(const MasterDateTime *time)
{
    if ((time == 0) || (time->year < 2000U) || (time->year > 2099U) ||
        (time->month == 0U) || (time->month > 12U) || (time->hour > 23U) ||
        (time->minute > 59U) || (time->day == 0U) ||
        (time->day > MasterCalendar_DaysInMonth(time->year, time->month)))
    {
        return 0U;
    }
    return 1U;
}

int8_t MasterCalendar_Compare(const MasterDateTime *left, const MasterDateTime *right)
{
    uint8_t index;

    if ((left == 0) || (right == 0))
    {
        return 0;
    }
    /* 将所有字段按年月日时分逐一比较，避免依赖结构体对齐。 */
    for (index = 0U; index < 5U; index++)
    {
        uint16_t l = (index == 0U) ? left->year :
                     (index == 1U) ? left->month :
                     (index == 2U) ? left->day :
                     (index == 3U) ? left->hour : left->minute;
        uint16_t r = (index == 0U) ? right->year :
                     (index == 1U) ? right->month :
                     (index == 2U) ? right->day :
                     (index == 3U) ? right->hour : right->minute;
        if (l < r) return -1;
        if (l > r) return 1;
    }
    return 0;
}

void MasterCalendar_AddMinutes(MasterDateTime *time, uint32_t minutes)
{
    uint8_t days;

    if ((time == 0) || (MasterCalendar_IsValid(time) == 0U))
    {
        return;
    }
    while (minutes != 0U)
    {
        time->minute++;
        if (time->minute >= 60U)
        {
            time->minute = 0U;
            time->hour++;
            if (time->hour >= 24U)
            {
                time->hour = 0U;
                time->day++;
                days = MasterCalendar_DaysInMonth(time->year, time->month);
                if (time->day > days)
                {
                    time->day = 1U;
                    time->month++;
                    if (time->month > 12U)
                    {
                        time->month = 1U;
                        time->year++;
                    }
                }
            }
        }
        minutes--;
    }
}

void MasterCalendar_ToWords(const MasterDateTime *time, uint16_t words[5])
{
    words[0] = time->year;
    words[1] = time->month;
    words[2] = time->day;
    words[3] = time->hour;
    words[4] = time->minute;
}

void MasterCalendar_FromWords(MasterDateTime *time, const uint16_t words[5])
{
    time->year = words[0];
    time->month = (uint8_t)words[1];
    time->day = (uint8_t)words[2];
    time->hour = (uint8_t)words[3];
    time->minute = (uint8_t)words[4];
}
