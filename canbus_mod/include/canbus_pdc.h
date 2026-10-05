#ifndef CANBUS_PDC_H
#define CANBUS_PDC_H

#include <stdint.h>
#include "canbus_slcan.h"

/* BMW E90 PDC CAN identifiers. */
#define CAN_ID_PDC_VALUE 0x1c2
#define CAN_ID_PDC_STATE 0x24a

/*
 * pdc_sensors[]
 * [0] front left 1     CAN 0x1C2 [4]
 * [1] front left 2     CAN 0x1C2 [5]
 * [2] front right 2    CAN 0x1C2 [6]
 * [3] front right 1    CAN 0x1C2 [7]
 * [4] rear right 1     CAN 0x1C2 [3]
 * [5] rear right 2     CAN 0x1C2 [2]
 * [6] rear left 2      CAN 0x1C2 [1]
 * [7] rear left 1      CAN 0x1C2 [0]
 */
enum pdc_sensor {
	PDC_FL, PDC_FLM, PDC_FRM, PDC_FR,
	PDC_RR, PDC_RRM, PDC_RLM, PDC_RL,
	PDC_SENSOR_COUNT
};

struct canbus_ui_state {
	uint8_t pdc_cm[PDC_SENSOR_COUNT];
	uint8_t back_active;
	uint32_t generation;
};

/* Apply a validated SLCAN frame; return 1 only when the UI state changes. */
int canbus_pdc_apply(struct canbus_ui_state *state,
		     const struct canbus_frame *frame);

#define CANBUS_IOC_ENTER_BACK	1
#define CANBUS_IOC_LEAVE_BACK	2
#define CANBUS_IOC_SET_RADAR	3	/* pbuffer: pdc_sensors[8] */

#endif
