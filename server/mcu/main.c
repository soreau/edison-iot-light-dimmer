/*
 * AC dimmer control code for Intel Edison MCU
 *
 * Implements lead edge cutting method for AC dimming
 * using this circuit:
 *
 *
 * == Circuit ==
 *
 *
 *     *  = connection
 *     |  = wire
 *     -  = wire
 *     '  = wire
 *     °  = polarity marking (pin 1)
 *     o  = component pin
 *     x  = connector pin
 *    <>  = resistor
 *    ()  = label
 * 
 * 
 *         _              (MOC3021)          (BT136)         _
 *        | |            *--o°  o--*            o--------*  | |
 *   (IN) |x|--<220ohm>--'  o-* o  '--<560ohm>--o--*     '--|x| + (LOAD)
 *        | |               o | o---------------o  |        | |
 *  (OUT) |x|------------*    |                    |        |x| -
 *        | |            |  o-* o                  |        |||   (NEUTRAL)
 *(+5VDC) |x|--<10Kohm>--*--o | o--------------<33Kohm>-----|x| -
 *        | |               o |°o--*               |        | |
 *  (GND) |x|--*         (H11AA1)  '-----------<33Kohm>--*--|x| + (MAINS)
 *        |_|  '--------------'                    '-----'  |_|   (120VAC)
 *
 *
 * 
 *
 * Polls pin 2 which is connected to the output of the
 * circuit. It is set high at the point when the sine
 * wave crosses zero, which happens twice per cycle.
 * For example at 60Hz, zero cross happens 120 times
 * per second. Pin 3 is connected to the input of the
 * circuit and set high to fire the triac, which is
 * turned off at every zero cross. The time between
 * zero cross (when pin 2 reads high) and firing the
 * triac (driving pin 3 high) is key for accurate
 * dimming and avoiding flicker when dimming lights,
 * which is the primary focus.
 *
 * The dim is a microsecond delay value and ranges
 * from 0 (brightest) to 8333 (off). Since we're
 * using 7 pins for input values, there are 128 steps
 * ranging from 0-127. Dividing the max dim by the
 * max input value and subtracting from the max dim
 * value is applies the input value linearly, where
 * r is the input value: 8333 - (r * (8333 / 127))
 * This gives a consistent voltage step but yields
 * many values in the higher and lower ranges which
 * have no visible effect. There is no floating point
 * available here so for more a better range that
 * feels more natural, we can use this:
 * (7000 - (((r * 55) * (r * 55)) / 7000)) + 1000
 *
 * The input value is the result of reading pins 6-13,
 * which are written by a user space program running on
 * the Edison and passes the dim level here, to the MCU.
 * Pin 13 is set high to signal the data pins 6-12 are
 * ready for reading. Here are some depictions of the
 * wave at different dim levels.
 *
 *
 * == AC Leading Edge Phase Cutting ==
 *
 *
 *    ~~~ = dim delay
 *     x  = zero cross
 *     *  = triac firing
 *    --- = zero reference
 *
 *
 * 50% brightness: r = 63, dim = 4200
 *
 *        ,                     ,
 *       | \                   | \
 *       |  \                  |  \
 *       |   \                 |   \
 * x~~~~~*----x~~~~~*----x~~~~~*----x~~~~~*----x
 *                  |   /                 |   /
 *                  |  /                  |  /
 *                  | /                   | /
 *                   "                     "
 *
 *
 * 30% brightness: r = 37, dim = 5905
 *
 *
 *         |.                    |.
 *         | \                   | \
 * x~~~~~~~*--x~~~~~~~*--x~~~~~~~*--x~~~~~~~*--x
 *                    | /                   | /
 *                    |'                    |'
 *
 *
 *
 * 90% brightness: r = 114, dim = 853
 *
 *       _                     _
 *     /   \                 /   \
 *    /     \               /     \
 *   |       \             |       \
 * x~*--------x~*--------x~*--------x~*--------x
 *              |       /             |       /
 *               \     /               \     /
 *                \   /                 \   /
 *                  "                     "
 *
 * Copyright (c) 2016 Scott Moreau
 *
 */


#include "mcu_api.h"

#define MAX_DIM 8000

void mcu_main()
{
	unsigned short dim = MAX_DIM;
	unsigned short to = MAX_DIM;
	unsigned short i = 0;

	gpio_setup(128, 0); /* set GPIO 2 as input */
	gpio_setup(12,  1); /* set GPIO 3 as output */
	gpio_write(12,  0); /* set GPIO 3 low */

	/* Hardware read for dim value */
	gpio_setup(182, 0); /* set GPIO 6 as input */
	gpio_setup(48,  0); /* set GPIO 7 as input */
	gpio_setup(49,  0); /* set GPIO 8 as input */
	gpio_setup(183, 0); /* set GPIO 9 as input */
	gpio_setup(41,  0); /* set GPIO 10 as input */
	gpio_setup(43,  0); /* set GPIO 11 as input */
	gpio_setup(42,  0); /* set GPIO 12 as input */

	/* Read trigger */
	gpio_setup(40, 1); /* set GPIO 13 as input */

	while (1) {
		if (i++ > 25) {
			i = 0;
			/* Check if the data pins are ready for reading */
			if (gpio_read(40)) {
				/* Read the 7 data pins into a variable. */
				unsigned short r;
				r  = gpio_read(182) << 0;
				r += gpio_read(48)  << 1;
				r += gpio_read(49)  << 2;
				r += gpio_read(183) << 3;
				r += gpio_read(41)  << 4;
				r += gpio_read(43)  << 5;
				r += gpio_read(42)  << 6;
				if (r) {
					/* Adjust so there are more values
					 * in the lower range and less in
					 * the higher range. No floating
					 * point available here. */
					unsigned short v = r * 55;
					to = (7000 - ((v * v) / 7000)) + 1000;
				} else {
					/* Off */
					to = MAX_DIM;
				}
			}
		}

		/* Slight brief fade transition */
		if (dim < to)
			dim += ((dim < to) ? 1 : -1);
		else
			dim = to;

		if (dim == to && to == MAX_DIM) {
				/* No need to set the triac pin,
				 * continue polling for new data */
				continue;
		}
		if (gpio_read(128)) {	/* wave is crossing zero if pin 2 is high */
			mcu_delay(dim);     /* fire triac after this delay */
			gpio_write(12, 1);  /* set GPIO 3 high */
			gpio_write(12, 0);  /* set GPIO 3 low */
		}
	}
}
