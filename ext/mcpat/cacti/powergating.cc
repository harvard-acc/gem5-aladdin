/*****************************************************************************
 *                                McPAT/CACTI
 *                      SOFTWARE LICENSE AGREEMENT
 *            Copyright 2012 Hewlett-Packard Development Company, L.P.
 *                          All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.”
 *
 ***************************************************************************/

#include "area.h"
#include "powergating.h"
#include "parameter.h"
#include <iostream>
#include <math.h>
#include <assert.h>

using namespace std;

/*
 * Sizing of sleep tx is independent of sleep/power-saving supply voltage, sleep/power-saving supply voltage only affects wake-up energy and time
 *
 * While using DSTN (Distributed sleep tx network), worst case sizing is used.
 * For DSTN, the network can help to reduce the runtime latency (or achieve the same latency with smaller transistor size)
 * For example, during write access, if not all bits are toggled, the sleep tx in the non-toggled path can work as the extra
 * discharge paths of all the toggled bits, in addition to the sleep tx in the bitlines with the toggled bits. Since CACTI itself
 * assumes worst case with all bits toggled, sleep txs are assumed to work all the time with all bits toggled,
 * Therefore, although DTSN is used, for memory array, the number of sleep txs is related to the number of rows and cols.,
 * and all calculations are still base on single sleep tx for each discharge case. Of couse in each discharge path, the sleep
 * tx is the charge path of all the devices in the same path (row or col).
 *
 * Even in the worse case sizing, the wakeup time will not change
 * since all paths need to charge/discharge---each sleep tx is just do its own portion of the work during wakeup or entering sleep state.
 *
 * Power-gating and DVS cannot happen at the same time! Because power-gating happens when circuit is idle,
 * while DVS happens when circuit is active.
 * When waking up from power-gating status, it is assumed that the system will first wakeup to DVS0 (full speed) state, if DVS is enabled in
 * the system.
 *
 *
 *
*/
Sleep_tx::Sleep_tx(
			double  _perf_with_sleep_tx,
			double  _active_Isat,//of circuit block, not sleep tx
            bool    _is_footer,
            double _c_circuit_wakeup,
            double _V_delta,
            int     _num_sleep_tx,
//            double  _vt_circuit,
//			double  _vt_sleep_tx,
//			double  _mobility,//of sleep tx
//			double  _c_ox,//of sleep tx
			const  Area & cell_)
:perf_with_sleep_tx(_perf_with_sleep_tx),
 active_Isat(_active_Isat),
 is_footer(_is_footer),
 c_circuit_wakeup(_c_circuit_wakeup),
 V_delta(_V_delta),
 num_sleep_tx(_num_sleep_tx),
// vt_circuit(_vt_circuit),
// vt_sleep_tx(_vt_sleep_tx),
// mobility(_mobility),
// c_ox(_c_ox)
 cell(cell_),
 is_sleep_tx(true)
{

	//a single sleep tx in a network
	double raw_area, raw_width, raw_hight;
	double p_to_n_sz_ratio = pmos_to_nmos_sz_ratio(false, false, true);
    vdd = g_tp.peri_global.Vdd;
    vt_circuit = g_tp.peri_global.Vth;
    vt_sleep_tx = g_tp.sleep_tx.Vth;
    mobility = g_tp.sleep_tx.Mobility_n;
    c_ox = g_tp.sleep_tx.C_ox;

    width = active_Isat/(perf_with_sleep_tx*mobility*c_ox*(vdd-vt_circuit)*(vdd-vt_sleep_tx))*g_ip->F_sz_um;//W/L uses physical numbers
    width /= num_sleep_tx;

//    double cell_hight = MAX(cell.w*2, g_tp.cell_h_def);
    raw_area   = compute_gate_area(INV, 1, width, p_to_n_sz_ratio*width, cell.h)/2; //Only single device, assuming device is laid on the side of the circuit block without changing the height of the standard library cells (using the standard cell approach).
    raw_width = cell.w;
    raw_hight  = raw_area/cell.w;
    area.set_h(raw_hight);
    area.set_w(raw_width);

    compute_penalty();

}

double Sleep_tx::compute_penalty()
{
	//V_delta = VDD - VCCmin nothing to do with threshold of sleep tx. Although it might be OK to use sleep tx to control the V_delta
	double c_load;
	double p_to_n_sz_ratio = pmos_to_nmos_sz_ratio(false, false, true);

	if (is_footer)
	{
		c_intrinsic_sleep = drain_C_(width, NCH, 1, 1, area.h, false, false, false,is_sleep_tx);
//		V_delta = _V_delta;
		wakeup_delay = (c_circuit_wakeup + c_intrinsic_sleep)*V_delta/(simplified_nmos_Isat(width, false, false, false,is_sleep_tx)/Ilinear_to_Isat_ratio);
		wakeup_power.readOp.dynamic = (c_circuit_wakeup + c_intrinsic_sleep)*g_tp.sram_cell.Vdd*V_delta;
		//no 0.5 because the half of the energy spend in entering sleep and half of the energy will be spent in waking up. And they are pairs
	}
	else
	{
		c_intrinsic_sleep = drain_C_(width*p_to_n_sz_ratio, PCH, 1, 1, area.h, false, false, false,is_sleep_tx);
//		V_delta = _V_delta;
		wakeup_delay = (c_circuit_wakeup + c_intrinsic_sleep)*V_delta/(simplified_pmos_Isat(width, false, false, false,is_sleep_tx)/Ilinear_to_Isat_ratio);
		wakeup_power.readOp.dynamic = (c_circuit_wakeup + c_intrinsic_sleep)*g_tp.sram_cell.Vdd*V_delta;
	}

/*
	The number of cycles in the wake-up latency set the constraint on the
	minimum number of idle clock cycles needed before a processor
	can enter in the corresponding sleep mode without any wakeup
	overhead.

	If the circuit is half way to sleep then waken up, it is still OK
	just the wakeup latency will be shorter than the wakeup time from full asleep.
	So, the sleep time and energy does not matter
*/

}

