#include "stepper.h"
#include "platform.h"
#include "messages.h"
#include "ums.h"
#include <stdlib.h>

typedef struct
{
	uint8_t stepPort;
	uint8_t stepPin;
	uint8_t dirPort;
	uint8_t dirPin;
	uint8_t fwdPort;
	uint8_t fwdPin;
	uint8_t revPort;
	uint8_t revPin;
} Axis_t;

struct
{
	Axis_t x;
	Axis_t y;
	Axis_t z;
	Axis_t u;
} Axes;

Axis_t *getAxis(char axisName)
{
	Axis_t *ret = NULL;
	switch (axisName) {
	case 'x':
	case 'X':
		ret = &Axes.x;
		break;
	case'y':
	case 'Y':
		ret = &Axes.y;
		break;
	case 'z':
	case 'Z':
		ret = &Axes.z;
		break;
	case 'u':
	case 'U':
		ret = &Axes.u;
		break;
	};

	return ret;
}

#define STEP_FIFO_SIZE 8
uint8_t stepFIFO[STEP_FIFO_SIZE];
uint32_t delayFIFO[STEP_FIFO_SIZE];
uint8_t stepHead, stepTail;
uint32_t prevDelay;
struct
{
    int adx, ady, adz, adu, adt;
    int ex, ey, ez, eu, et;
    int maxd;
    int count;
    uint32_t stepDelay;
    uint8_t dir, step, delayInc;

} Line;


void st_line_setup(int dx, int dy, int dz, int du, uint32_t delay)
{
    Line.adx = abs(dx);
    Line.ady = abs(dy);
    Line.adz = abs(dz);
    Line.adu = abs(du);

    Line.dir = 0;
    Line.dir |= (dx < 0) ? 0 : UMS_X_DIR;
    Line.dir |= (dy < 0) ? 0 : UMS_Y_DIR;
    Line.dir |= (dz < 0) ? 0 : UMS_Z_DIR;
    Line.dir |= (du < 0) ? 0 : UMS_U_DIR;

    Line.maxd = Line.adx;
    Line.maxd = Line.ady > Line.maxd ? Line.ady : Line.maxd;
    Line.maxd = Line.adz > Line.maxd ? Line.adz : Line.maxd;
    Line.maxd = Line.adu > Line.maxd ? Line.adu : Line.maxd;

    // dither delay between steps to get exactly the right delay
    if (Line.maxd != 0) {
    	Line.stepDelay = delay / Line.maxd;
    } else {
    	Line.stepDelay = delay;
    }
    Line.adt = delay - (Line.stepDelay * Line.maxd);

    Line.ex = Line.ey = Line.ez = Line.eu = 0;
    Line.count = 0;
    Line.step = 0;
    Line.delayInc = 0;
}

uint8_t st_line_next_step()
{
    // return 0 if there we are at the end of the line
    if (Line.count == Line.maxd) {
        return 0;
    }

    Line.step = Line.dir;

    Line.ex += 2 * Line.adx;
    if (Line.ex > Line.maxd) {
        Line.step |= UMS_X_STEP;
        Line.ex -= 2 * Line.maxd;
    }

    Line.ey += 2 * Line.ady;
    if (Line.ey > Line.maxd) {
    	Line.step |= UMS_Y_STEP;
        Line.ey -= 2 * Line.maxd;
    }

    Line.ez += 2 * Line.adz;
    if (Line.ez > Line.maxd) {
    	Line.step |= UMS_Z_STEP;
        Line.ez -= 2 * Line.maxd;
    }

    Line.eu += 2 * Line.adu;
    if (Line.eu > Line.maxd) {
    	Line.step |= UMS_U_STEP;
        Line.eu -= 2 * Line.maxd;
    }

    Line.et += 2 * Line.adt;
    Line.delayInc = 0;
    if (Line.et > Line.maxd) {
    	Line.et -= 2 * Line.maxd;
    	Line.delayInc = 1;
    }

    Line.count++;

    return 1;
}

void st_init( )
{
	pf_init_axes();

    // initialize step fifo
    stepHead = stepTail = 0;

    // no active line
    st_line_setup(0, 0, 0, 0, 1000);

    umsStepCounter = 0;
}

uint8_t st_full()
{
    // return 1 if full
	if  ((stepTail == stepHead+1) ||
			(stepTail == 0 && stepHead == STEP_FIFO_SIZE-1)) {
		return 1;
	}

	// if not full, try to fill up with steps from active line
	else {
	    while (st_line_next_step() != 0) {
	        st_add_step(Line.step, Line.stepDelay + Line.delayInc);
	        if  ((stepTail == stepHead+1) ||
	                (stepTail == 0 && stepHead == STEP_FIFO_SIZE-1)) {
	            return 1;
	        }
	    }
		return 0;
	}
}

void st_add_step(uint8_t stepDir, uint32_t delay)
{
	uint8_t p;
	stepFIFO[stepHead]  = stepDir;
	delayFIFO[stepHead] = delay;
	p = stepHead+1;
	if (p == STEP_FIFO_SIZE)
		stepHead = 0;
	else
		stepHead = p;

	// need to start the timer if it isn't already running so this step will happen
	if (pf_is_timer_running() == 0) {
		prevDelay = 1;
		pf_set_step_timer(1);
		umsStatus |= UMS_STEPPER_RUNNING;
		umsStatus |= UMS_SEND_STATUS_NOW;

	}
}

/**
 * Set the level on an output pin taking inversion into account.
 */
void st_set_ipin(uint8_t port, uint8_t ipin, uint8_t val)
{
	uint8_t pin = ipin & ~UMS_INVERT_PIN;
	if (pin != ipin) {
		val = (val==0) ? 1 : 0;
	}
	pf_set_port_pin(port, pin, val);
}

/**
 * Read an input pin and invert result if necessary.
 */
uint8_t st_read_ipin(uint8_t port, uint8_t ipin)
{
	uint8_t pin = ipin & ~UMS_INVERT_PIN;
	uint8_t val = pf_read_port_pin(port, pin);
	if (pin != ipin) {
		val = (val==0) ? 1 : 0;
	}
	return val;
}

#define UMS_STEP_BIT 0x01
#define UMS_DIR_BIT  0x02
#define UMS_NORMAL_STEP 0
#define UMS_FWD_LIMIT   1
#define UMS_REV_LIMIT   2
/**
 * Generate a step in the specified direction on the specified axis.
 * \param stepDir bit0 is step, bit 1 is dir.
 * \return UMS_NORMAL_STEP for normal step, UMS_FWD_LIMIT for forward limit, UMS_REV_LIMIT for reverse limit
 */
uint8_t st_do_step(Axis_t *a, uint8_t stepDir, int32_t *counter)
{
	int8_t counterDir = 0;

	// do nothing if this axis has no step dir pins assigned or step is not set
	if (a->dirPin == UMS_UNASSIGNED_PORT || a->stepPin == UMS_UNASSIGNED_PORT || (stepDir & UMS_STEP_BIT) == 0)
		return UMS_NORMAL_STEP;

	// set direction bit first
	st_set_ipin(a->dirPort, a->dirPin, stepDir & UMS_DIR_BIT);

	// moving forward
	if ((stepDir & UMS_DIR_BIT) != 0) {
		counterDir = 1;
		// read limit pin if there is one
		if (a->fwdPort != UMS_UNASSIGNED_PORT) {
			uint8_t limit = st_read_ipin(a->fwdPort, a->fwdPin);
			if (limit != 0) {
				// limit activated, no step
				return UMS_FWD_LIMIT;
			}
		}
	} else {
		counterDir = -1;
		// read limit pin if there is one
		if (a->revPort != UMS_UNASSIGNED_PORT) {
			uint8_t limit = st_read_ipin(a->revPort, a->revPin);
			if (limit != 0) {
				// limit activated, no step
				return UMS_REV_LIMIT;
			}
		}
	}

	// no limit so activate step pin and increment counter
	st_set_ipin(a->stepPort, a->stepPin, 1);
	*counter += counterDir;
	return UMS_NORMAL_STEP;
}

/**
 * Steps are edge driven so this clears the step outputs.
 */
void st_clear_steps()
{

	if (Axes.x.stepPort != UMS_UNASSIGNED_PORT)
		st_set_ipin(Axes.x.stepPort, Axes.x.stepPin, 0);
	if (Axes.y.stepPort != UMS_UNASSIGNED_PORT)
		st_set_ipin(Axes.y.stepPort, Axes.y.stepPin, 0);
	if (Axes.z.stepPort != UMS_UNASSIGNED_PORT)
		st_set_ipin(Axes.z.stepPort, Axes.z.stepPin, 0);
	if (Axes.u.stepPort != UMS_UNASSIGNED_PORT)
		st_set_ipin(Axes.u.stepPort, Axes.u.stepPin, 0);
}

void st_run_once()
{
	umsRunTime += prevDelay;

	// nothing to do if the FIFO is empty
	if (stepTail != stepHead) {
		uint8_t p, stepDir;
		umsStepCounter++;
		// read the next step from FIFO and increment tail
		stepDir = stepFIFO[stepTail];
		prevDelay = delayFIFO[stepTail];
		pf_set_step_timer(delayFIFO[stepTail]);
		p = stepTail + 1;
		if (p == STEP_FIFO_SIZE)
			stepTail = 0;
		else
			stepTail = p;

		umsLimits = 0;
		umsLimits |= st_do_step(&Axes.x, stepDir,       &umsXPos);
		umsLimits |= (st_do_step(&Axes.y, stepDir >> 2, &umsYPos) << 2);
		umsLimits |= (st_do_step(&Axes.z, stepDir >> 4, &umsZPos) << 4);
		umsLimits |= (st_do_step(&Axes.u, stepDir >> 6, &umsUPos) << 6);

		st_clear_steps();
	}

	// done, cause a status to be sent, clear running status, disable timer
	else {
		umsStatus &= ~UMS_STEPPER_RUNNING;
		umsStatus |= UMS_SEND_STATUS_NOW;
        pf_set_step_timer(0);
	}
}

void st_pin_config_error(uint8_t port, uint8_t pin)
{
	// error message to send if any of the configuration calls fail
	struct WarnMsg wmsg;
	wmsg.msgId = WarnMsg_ID;
	wmsg.warnId = UMS_WARN_CONFIGURE_PIN;
	wmsg.data[0] = port;
	wmsg.data[1] = pin;
	pf_send_bytes((uint8_t*)&wmsg, WarnMsg_LENGTH);
}

void st_setup_axis(struct AxisCmd *c)
{
	uint8_t e = 0;
	Axis_t *axis = getAxis(c->name);

	if (axis == NULL) {
		struct WarnMsg wmsg;
		wmsg.msgId = ErrorMsg_ID;
		wmsg.warnId = UMS_WARN_BAD_AXIS;
		wmsg.data[0] = c->name;
		pf_send_bytes((uint8_t*)&wmsg, WarnMsg_LENGTH);
		return;
	}

	axis->stepPort = c->stepPort;
	axis->stepPin  = c->stepPin;
	e = pf_configure_port_pin(axis->stepPort, axis->stepPin & ~UMS_INVERT_PIN, UMS_OUTPUT_PIN);
	if (e != 0) st_pin_config_error(axis->stepPort, axis->stepPin);

	axis->dirPort  = c->dirPort;
	axis->dirPin   = c->dirPin;
    e = pf_configure_port_pin(axis->dirPort, axis->dirPin & ~UMS_INVERT_PIN, UMS_OUTPUT_PIN);
	if (e != 0) st_pin_config_error(axis->dirPort, axis->dirPin);

    // configure the limit pins with a pullup or pulldown to be inactive when disconnected
    axis->fwdPort  = c->fwdPort;
	axis->fwdPin   = c->fwdPin;
	if (axis->fwdPin & UMS_INVERT_PIN) {
	    e = pf_configure_port_pin(axis->fwdPort, axis->fwdPin & ~UMS_INVERT_PIN, UMS_INPUT_PULLUP_PIN);
	} else {
        e = pf_configure_port_pin(axis->fwdPort, axis->fwdPin & ~UMS_INVERT_PIN, UMS_INPUT_PULLDOWN_PIN);
	}
	if (e != 0) st_pin_config_error(axis->fwdPort, axis->fwdPin);

	axis->revPort  = c->revPort;
	axis->revPin   = c->revPin;
    if (axis->revPin & UMS_INVERT_PIN) {
        e = pf_configure_port_pin(axis->revPort, axis->revPin & ~UMS_INVERT_PIN, UMS_INPUT_PULLUP_PIN);
    } else {
        e = pf_configure_port_pin(axis->revPort, axis->revPin & ~UMS_INVERT_PIN, UMS_INPUT_PULLDOWN_PIN);
    }
	if (e != 0) st_pin_config_error(axis->revPort, axis->revPin);

	st_clear_steps();
}
