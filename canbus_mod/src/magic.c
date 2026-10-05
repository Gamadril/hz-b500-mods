#include <mod_defs.h>

extern int32_t CANBUS_MInit(void);
extern int32_t CANBUS_MExit(void);
extern __mp *CANBUS_MOpen(uint32_t id, uint32_t mode);
extern int32_t CANBUS_MClose(__mp *mp);
extern uint32_t CANBUS_MRead(void *pdata, uint32_t size, uint32_t n, __mp *mp);
extern uint32_t CANBUS_MWrite(const void *pdata, uint32_t size, uint32_t n, __mp *mp);
extern long CANBUS_MIoctrl(__mp *mp, uint32_t cmd, int32_t aux, void *pbuffer);

const __module_mgsec_t modinfo __attribute__((section(".magic"))) =
{
	{'e', 'P', 'D', 'K', '.', 'm', 'o', 'd'},
	0x01000000,
	/* USER(0xff) → MODS_GetModuleID aborts: "user type module not allowd on melis3.0" */
	0x8d,
	0xF0000,
	0x400,
	{
		&CANBUS_MInit,
		&CANBUS_MExit,
		&CANBUS_MOpen,
		&CANBUS_MClose,
		&CANBUS_MRead,
		&CANBUS_MWrite,
		&CANBUS_MIoctrl
	}
};
