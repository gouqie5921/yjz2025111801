/**
  ******************************************************************************
  * @file    dbg.c
  * @brief   极简串口调试输出实现（USART2, 115200）
  ******************************************************************************
  */
#include "dbg.h"
#include "usart.h"
#include "main.h"

#define DBG_UART        huart2
#define DBG_TIMEOUT_MS  100u

static void put(const char *s, uint16_t len)
{
    (void)HAL_UART_Transmit(&DBG_UART, (uint8_t *)s, len, DBG_TIMEOUT_MS);
}

void dbg_str(const char *s)
{
    uint16_t n = 0u;

    if (s == 0)
    {
        return;
    }
    while (s[n] != '\0')
    {
        n++;
    }
    if (n > 0u)
    {
        put(s, n);
    }
}

void dbg_u32(uint32_t v)
{
    char    tmp[11];
    uint8_t i = 0u;

    if (v == 0u)
    {
        put("0", 1u);
        return;
    }

    while ((v > 0u) && (i < sizeof(tmp)))
    {
        tmp[i] = (char)('0' + (v % 10u));
        v /= 10u;
        i++;
    }
    while (i > 0u)
    {
        i--;
        put(&tmp[i], 1u);
    }
}

void dbg_i32(int32_t v)
{
    if (v < 0)
    {
        char m = '-';
        put(&m, 1u);
        /* 注意 -(-2147483648) 会溢出，先转 uint32 */
        dbg_u32((uint32_t)(-(int64_t)v));
    }
    else
    {
        dbg_u32((uint32_t)v);
    }
}

void dbg_f(float v, uint8_t decimals)
{
    uint32_t scale = 1u;
    uint8_t  i;
    int32_t  scaled;
    uint32_t ip;
    uint32_t fp;

    if (decimals > 4u)
    {
        decimals = 4u;
    }
    for (i = 0u; i < decimals; i++)
    {
        scale *= 10u;
    }

    /* 四舍五入到定点 */
    if (v < 0.0f)
    {
        char m = '-';
        put(&m, 1u);
        v = -v;
    }
    scaled = (int32_t)(v * (float)scale + 0.5f);

    ip = (uint32_t)scaled / scale;
    fp = (uint32_t)scaled % scale;

    dbg_u32(ip);
    if (decimals > 0u)
    {
        char dot = '.';
        put(&dot, 1u);

        /* 前导零补齐 */
        {
            uint32_t lead = scale / 10u;
            while ((lead > 1u) && (fp < lead))
            {
                char z = '0';
                put(&z, 1u);
                lead /= 10u;
            }
        }
        dbg_u32(fp);
    }
}

void dbg_hex8(uint8_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char s[4];

    s[0] = '0';
    s[1] = 'x';
    s[2] = hx[(v >> 4) & 0x0Fu];
    s[3] = hx[v & 0x0Fu];
    put(s, 4u);
}

void dbg_nl(void)
{
    put("\r\n", 2u);
}
