// ============================================================================
// 文件：main.cpp
// 职责：上电初始化与主循环调度（不含业务逻辑）
//   setup 阶段：时钟 → 灯效/日志 → 数据模型 → Flash → 压力 ADC → 标定
//               → 运动控制 → 总线初始化
//   主循环    ：收总线帧 → 跑 aHub / BambuBus 协议 → 发应答
//               → 状态落盘 → 运动控制 → 灯效
// 依赖：ams / Flash_saves / ADC_DMA / Motion_control / bambu_bus_ams / ahub_bus
// 注意：主循环里 motion_control_run() 每轮必跑，其余按需；不要在循环里做长阻塞
// ============================================================================
#include "MC_PULL_calibration.h"
#include "ws2812.h"

#include "Flash_saves.h"
#include "Motion_control.h"
#include "ams_addressing.h"
#include "_bus_hardware.h"
#include "ams.h"
#include "app_api.h"
#include "ahub_bus.h"
#include "bambu_bus_ams.h"
#include "ADC_DMA.h"
#include "Debug_log.h"
#include <string.h>

// ==================== P2-1 服务表 / 慢 tick / 启动分段打点 ====================
// 只在 -DDebug_log_on 的台架调试固件里编译；正式固件整块被预处理掉，零 flash 零耗时。
// 用法：pio run -e ams_b_090_soft_dbg ，串口 UART3（PB10/PB11，115200 8E1）看输出。
#ifdef Debug_log_on
enum
{
    BM_SVC_RX = 0,
    BM_SVC_AHUB,
    BM_SVC_BAMBU,
    BM_SVC_TX,
    BM_SVC_HOST,
    BM_SVC_SAVE,
    BM_SVC_MOTION,
    BM_SVC_FLASH,
    BM_SVC_RGB,
    BM_SVC_N
};

typedef struct { const char* name; uint32_t slowUs; } bm_svc_t;

// 慢 tick 阈值（µs）：只报超阈值的，避免刷屏
static const bm_svc_t kSvc[BM_SVC_N] = {
    { "bus_rx_poll",    2000u },    // DMA 取字节 + 帧同步
    { "ahubus_run",     3000u },    // 旧 aHub 协议
    { "bambubus_run",   5000u },    // BambuBus 协议（含注册握手）
    { "send_package",   3000u },    // 发应答（等 DE/TC）
    { "host_type",       500u },    // 主机类型判断 + 状态灯
    { "save_flash",   150000u },    // Flash 写（仅后台空闲时）
    { "motion_run",     5000u },    // 运动控制
    { "flash_bg",     150000u },    // Flash 后台任务
    { "rgb_update",     3000u },    // 灯带刷新
};

static uint32_t s_svcT0[BM_SVC_N];
static uint32_t s_svcMax[BM_SVC_N];
static uint32_t s_bootTick  = 0u;   // 分段打点：上一段的结束
static uint32_t s_bootFirst = 0u;   // 启动总耗时起点

static uint32_t bm_ticks_us(uint32_t d)
{
    const uint32_t tpu = time_hw_tpus;
    return tpu ? d / tpu : 0u;
}

// 手拼一行 "<tag> <name>=<us>us" —— 不用 printf，省 flash
static void bm_log_line(const char* tag, const char* name, uint32_t us)
{
    char out[64];
    uint8_t n = 0;
    out[n++] = ' ';
    while (*tag && n < 10u) out[n++] = *tag++;
    out[n++] = ' ';
    while (*name && n < 44u) out[n++] = *name++;
    out[n++] = '=';

    char dig[10];
    uint8_t d = 0;
    if (!us) dig[d++] = '0';
    while (us) { dig[d++] = (char)('0' + (us % 10u)); us /= 10u; }
    while (d && n < 56u) out[n++] = dig[--d];

    const char* u = "us\n";
    while (*u && n < 62u) out[n++] = *u++;
    DEBUG_num(out, (int)n);
}

static inline void svc_begin(uint8_t i) { s_svcT0[i] = time_ticks32(); }

static inline void svc_end(uint8_t i)
{
    const uint32_t d = bm_ticks_us(time_diff_u32(time_ticks32(), s_svcT0[i]));
    if (d > s_svcMax[i]) s_svcMax[i] = d;
    if (d > kSvc[i].slowUs) bm_log_line("[slow]", kSvc[i].name, d);
}

#define SVC_BEGIN(i)  svc_begin((uint8_t)(i))
#define SVC_END(i)    svc_end((uint8_t)(i))
#define BOOT_RESET()  do { s_bootFirst = s_bootTick = time_ticks32(); } while (0)
#define BOOT_MARK(n)  do { \
        const uint32_t _now = time_ticks32(); \
        bm_log_line("[boot]", (n), bm_ticks_us(time_diff_u32(_now, s_bootTick))); \
        s_bootTick = _now; \
    } while (0)
#define BOOT_TOTAL(n) do { \
        bm_log_line("[boot]", (n), bm_ticks_us(time_diff_u32(time_ticks32(), s_bootFirst))); \
    } while (0)
#else
#define SVC_BEGIN(i)  do {} while (0)
#define SVC_END(i)    do {} while (0)
#define BOOT_RESET()  do {} while (0)
#define BOOT_MARK(n)  do {} while (0)
#define BOOT_TOTAL(n) do {} while (0)
#endif

WS2812_class SYS_RGB;
WS2812_class RGBOUT[4];

void RGB_init()
{
    SYS_RGB.init(1, GPIOD, GPIO_Pin_1);
    RGBOUT[0].init(2, GPIOA, GPIO_Pin_11);
    RGBOUT[1].init(2, GPIOA, GPIO_Pin_8);
    RGBOUT[2].init(2, GPIOB, GPIO_Pin_1);
    RGBOUT[3].init(2, GPIOB, GPIO_Pin_0);
}

void RGB_update()
{
    if (!(SYS_RGB.is_dirty() ||
          RGBOUT[0].is_dirty() || RGBOUT[1].is_dirty() ||
          RGBOUT[2].is_dirty() || RGBOUT[3].is_dirty()))
        return;

    if (!bus_background_ready()) return;

    static uint32_t last = 0u;

    uint32_t min_gap = time_hw_tpms;
    if (!min_gap) min_gap = 1u;

    const uint32_t now = time_ticks32();
    if (last != 0u && (uint32_t)(now - last) < min_gap)
        return;

    last = now;

    SYS_RGB.updata();
    RGBOUT[0].updata();
    RGBOUT[1].updata();
    RGBOUT[2].updata();
    RGBOUT[3].updata();
}

static uint8_t g_fil_dirty = 0;
static uint8_t g_loaded_ch = 0xFF;
static uint8_t g_state_dirty = 0;
static uint8_t g_state_write_value = 0xFF;
static uint8_t g_loaded_flash_pending = 0;

static inline void ram_to_flashinfo(uint8_t fil, flash_filamentinfo* o)
{
    const _filament* f = &ams[BMCU_LOCAL_AMS_INDEX].filament[fil];

    memcpy(o->bambubus_filament_id, f->bambubus_filament_id, sizeof(o->bambubus_filament_id));
    o->color_R = f->color_R;
    o->color_G = f->color_G;
    o->color_B = f->color_B;
    o->color_A = f->color_A;
    o->temperature_min = f->temperature_min;
    o->temperature_max = f->temperature_max;
    memcpy(o->name, f->name, sizeof(o->name));
}

static inline void flashinfo_to_ram(uint8_t fil, const flash_filamentinfo* i)
{
    _filament* f = &ams[BMCU_LOCAL_AMS_INDEX].filament[fil];

    memcpy(f->bambubus_filament_id, i->bambubus_filament_id, sizeof(i->bambubus_filament_id));
    f->color_R = i->color_R;
    f->color_G = i->color_G;
    f->color_B = i->color_B;
    f->color_A = i->color_A;
    f->temperature_min = i->temperature_min;
    f->temperature_max = i->temperature_max;

    memset(f->name, 0, sizeof(f->name));
    memcpy(f->name, i->name, sizeof(i->name));
    f->name[sizeof(f->name) - 1u] = 0;
}

bool ams_datas_read()
{
    bool any = false;

    for (uint8_t fil = 0; fil < 4u; fil++)
    {
        flash_filamentinfo fi;
        if (flash_ams_filament_read(fil, &fi))
        {
            flashinfo_to_ram(fil, &fi);
            any = true;
        }
    }

    return any;
}

void ams_datas_set_need_to_save()
{
    g_fil_dirty = 0x0Fu;
}

void ams_datas_set_need_to_save_filament(uint8_t filament_idx)
{
    if (filament_idx >= 4u) return;
    g_fil_dirty |= (uint8_t)(1u << filament_idx);
}

void ams_state_claim_loaded(uint8_t filament_ch)
{
    if (filament_ch >= 4u) return;
    if (g_loaded_ch != 0xFFu && g_loaded_ch != filament_ch) return;

    if (g_loaded_ch == 0xFFu)
    {
        g_loaded_ch = filament_ch;
        g_loaded_flash_pending = 1u;
    }
}

void ams_state_set_loaded(uint8_t filament_ch)
{
    if (filament_ch >= 4u) return;
    if (g_loaded_ch != 0xFFu && g_loaded_ch != filament_ch) return;

    if (g_loaded_ch == 0xFFu)
    {
        g_loaded_ch = filament_ch;
        g_loaded_flash_pending = 1u;
    }

    if (!g_loaded_flash_pending) return;

    g_state_write_value = filament_ch;
    g_state_dirty = 1u;
    g_loaded_flash_pending = 0u;
}

void ams_state_set_unloaded(uint8_t filament_ch)
{
    if (g_loaded_ch == 0xFFu) return;
    if (filament_ch < 4u && g_loaded_ch != filament_ch) return;
    g_loaded_ch = 0xFFu;
    g_loaded_flash_pending = 0u;
    g_state_write_value = 0xFFu;
    g_state_dirty = 1u;
}

uint8_t ams_state_get_loaded(void)
{
    return g_loaded_ch;
}

static void ams_state_save_run()
{
    if (!g_state_dirty) return;

    if (flash_ams_state_write(g_state_write_value))
        g_state_dirty = 0u;
}

void ams_datas_save_run()
{
    if (!g_fil_dirty) return;

    uint8_t fil = 0xFFu;
    for (uint8_t i = 0; i < 4u; i++)
    {
        if (g_fil_dirty & (uint8_t)(1u << i))
        {
            fil = i;
            break;
        }
    }

    if (fil == 0xFFu) return;

    flash_filamentinfo now;
    ram_to_flashinfo(fil, &now);

    if (flash_ams_filament_write(fil, &now))
        g_fil_dirty &= (uint8_t)~(1u << fil);
}

int main(void)
{
    SystemInit();
    SystemCoreClockUpdate();
    time_hw_init();
    bm_diag_boot_init();        // 复位原因必须在其它初始化改动 RCC 标志前读
    BOOT_RESET();               // P2-1：从这里开始分段计时

    __enable_irq();

    WWDG_DeInit();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, DISABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    GPIO_PinRemapConfig(GPIO_Remap_PD01, ENABLE);

    RGB_init();
    delay(10);

    SYS_RGB.set_RGB(0x10, 0x00, 0x00, 0);
    for (int i = 0; i < 4; i++) RGBOUT[i].set_RGB(0, 0, 0, 0);
    RGB_update();
    delay(50);
    BOOT_MARK("rgb");

    DEBUG_init();
    ams_init();
    flash_saves_init();
    BOOT_MARK("ams+flash");

    ADC_DMA_init();
    ADC_DMA_wait_full();
    BOOT_MARK("adc");

    mc_pull_calibration_boot();
    ams_datas_read();
    BOOT_MARK("calib");

    {
        uint8_t ch = 0xFFu;
        if (flash_ams_state_read(&ch))
        {
            g_loaded_ch = ch;

            if (ch < 4u)
            {
                _ams* a = &ams[BMCU_LOCAL_AMS_INDEX];

                a->now_filament_num  = ch;
                a->filament_use_flag = 0x04;
                a->pressure          = 0x2B00;

                for (uint8_t i = 0; i < 4u; i++)
                    a->filament[i].motion = _filament_motion::idle;

                a->filament[ch].motion = _filament_motion::on_use;
            }
        }
    }

    motion_control_init();
    BOOT_MARK("motion");
    if (g_loaded_ch < 4u && (!motion_control_filament_present(g_loaded_ch) ||
        !mc_pull_calibration_is_valid(g_loaded_ch))) {
        _ams* a = &ams[BMCU_LOCAL_AMS_INDEX];
        a->filament[g_loaded_ch].motion = _filament_motion::idle;
        a->now_filament_num = 0xFFu;
        a->filament_use_flag = 0u;
        a->pressure = 0xF9C6u;
        ams_state_set_unloaded(g_loaded_ch);
    }
    bambubus_init();
    bus_init();
    BOOT_MARK("bus");

    DEBUG("START\n");
    BOOT_TOTAL("boot");

    while (1)
    {
        SVC_BEGIN(BM_SVC_RX);
        bus_rx_poll();
        SVC_END(BM_SVC_RX);

        SVC_BEGIN(BM_SVC_AHUB);
        const ahubus_package_type   ahub_stu     = ahubus_run();
        SVC_END(BM_SVC_AHUB);

        SVC_BEGIN(BM_SVC_BAMBU);
        const bambubus_package_type bambubus_stu = bambubus_run();
        SVC_END(BM_SVC_BAMBU);

        SVC_BEGIN(BM_SVC_TX);
        bus_port_to_host.send_package();
        SVC_END(BM_SVC_TX);

        SVC_BEGIN(BM_SVC_HOST);
        if (bambubus_stu != bambubus_package_type::none && bambubus_stu != bambubus_package_type::error)
            bus_host_device_type = host_device_type_ams;
        else if (ahub_stu != ahubus_package_type::none && ahub_stu != ahubus_package_type::error)
            bus_host_device_type = host_device_type_ahub;

        const int error = bus_host_device_type == host_device_type_ams
            ? (bambubus_stu == bambubus_package_type::error ? -1 : 0)
            : bus_host_device_type == host_device_type_ahub
                ? (ahub_stu == ahubus_package_type::error ? -1 : 0) : -1;
        if (bambubus_arrange_wait()) SYS_RGB.set_RGB(0x10, 0x00, 0x00, 0);
        else if (error) SYS_RGB.set_RGB(0x10, 0x00, 0x00, 0);
        else SYS_RGB.set_RGB(0x38, 0x35, 0x32, 0);
        SVC_END(BM_SVC_HOST);

        SVC_BEGIN(BM_SVC_SAVE);
        if (g_state_dirty && bus_background_ready()) ams_state_save_run();
        if (g_fil_dirty && bus_background_ready()) ams_datas_save_run();
        SVC_END(BM_SVC_SAVE);

        SVC_BEGIN(BM_SVC_MOTION);
        motion_control_run(error);
        SVC_END(BM_SVC_MOTION);

        SVC_BEGIN(BM_SVC_FLASH);
        flash_background_run();
        SVC_END(BM_SVC_FLASH);

        SVC_BEGIN(BM_SVC_RGB);
        RGB_update();
        SVC_END(BM_SVC_RGB);

        bm_diag_loop_tick();
    }
}
