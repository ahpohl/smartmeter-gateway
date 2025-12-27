/**
 * @file meter_registers.h
 * @brief SunSpec Meter Model registers (Integer + Scale Factor and Float) for
 * Fronius devices.
 *
 * @details
 * This header consolidates the symbolic register definitions for Fronius
 * SunSpec meters, supporting both floating-point (M21X) and integer + scale
 * factor (M20X) models.
 *
 * The two namespaces represent the two different representations:
 *
 * - **M21X::** Floating-point registers (M21X)
 *   - Measurements are 32-bit IEEE floats and can generally be used directly as
 * physical values.
 *   - Supported meter model IDs:
 *     - 211 – Single-phase
 *     - 212 – Split-phase
 *     - 213 – Three-phase
 *
 * - **M20X::** Integer + scale factor registers (M20X)
 *   - Measurements are stored as 16-bit integers and require the associated
 * scale factor registers (e.g., A_SF, V_SF) to convert to physical units.
 *   - Supported meter model IDs:
 *     - 201 – Single-phase
 *     - 202 – Split-phase
 *     - 203 – Three-phase
 *
 * @note Many registers in the integer + scale factor model require applying the
 * scale factor to obtain correct physical values, while float registers do not.
 */

#ifndef METER_REGISTERS_H_
#define METER_REGISTERS_H_

#include "register_base.h"
#include <cstdint>

/**
 * @namespace M20X
 * @brief Contains integer + scale factor SunSpec Meter registers for Fronius
 * inverters.
 *
 * All measurements in this namespace are represented as 16-bit integers. Scale
 * factors must be applied to convert raw register values into physical units.
 * Register addresses follow the standard SunSpec integer + scale factor model
 * layout.
 */
namespace M20X {

/** @brief Total number of registers in the meter model (integer + scale
 * factor). */
constexpr uint16_t SIZE = 105;

/**
 * @brief SunSpec meter model identifier
 * @return
 * - 201 – Single-phase meter
 * - 202 – Split-phase meter
 * - 203 – Three-phase meter
 */
constexpr Register ID(40069, 1, Register::Type::UINT16);

/**
 * @brief Length of meter model block
 * @return Always 105
 */
constexpr Register L(40070, 1, Register::Type::UINT16);

/**
 * @brief Total AC current
 * @unit Amperes [A]
 */
constexpr Register A(40071, 1, Register::Type::INT16);

/**
 * @brief AC current phase A
 * @unit Amperes [A]
 */
constexpr Register APHA(40072, 1, Register::Type::INT16);

/**
 * @brief AC current phase B
 * @unit Amperes [A]
 */
constexpr Register APHB(40073, 1, Register::Type::INT16);

/**
 * @brief AC current phase C
 * @unit Amperes [A]
 */
constexpr Register APHC(40074, 1, Register::Type::INT16);

/** @brief AC current scale factor */
constexpr Register A_SF(40075, 1, Register::Type::INT16);

/**
 * @brief Average AC voltage phase-to-neutral
 * @unit Volts [V]
 */
constexpr Register PHV(40076, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase A to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHA(40077, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase B to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHB(40078, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase C to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHC(40079, 1, Register::Type::INT16);

/**
 * @brief Average AC voltage phase-to-phase
 * @unit Volts [V]
 */
constexpr Register PPV(40080, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase AB
 * @unit Volts [V]
 */
constexpr Register PPVPHAB(40081, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase BC
 * @unit Volts [V]
 */
constexpr Register PPVPHBC(40082, 1, Register::Type::INT16);

/**
 * @brief AC voltage phase CA
 * @unit Volts [V]
 */
constexpr Register PPVPHCA(40083, 1, Register::Type::INT16);

/** @brief Voltage scale factor */
constexpr Register V_SF(40084, 1, Register::Type::INT16);

/**
 * @brief AC frequency
 * @unit Hertz [Hz]
 */
constexpr Register FREQ(40085, 1, Register::Type::INT16);

/** @brief Frequency scale factor */
constexpr Register FREQ_SF(40086, 1, Register::Type::INT16);

/**
 * @brief Total AC active power
 * @unit Watts [W]
 */
constexpr Register W(40087, 1, Register::Type::INT16);

/**
 * @brief AC active power phase A
 * @unit Watts [W]
 */
constexpr Register WPHA(40088, 1, Register::Type::INT16);

/**
 * @brief AC active power phase B
 * @unit Watts [W]
 */
constexpr Register WPHB(40089, 1, Register::Type::INT16);

/**
 * @brief AC active power phase C
 * @unit Watts [W]
 */
constexpr Register WPHC(40090, 1, Register::Type::INT16);

/** @brief Power scale factor */
constexpr Register W_SF(40091, 1, Register::Type::INT16);

/**
 * @brief Total AC apparent power
 * @unit Volt-amperes [VA]
 */
constexpr Register VA(40092, 1, Register::Type::INT16);

/**
 * @brief AC apparent power phase A
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHA(40093, 1, Register::Type::INT16);

/**
 * @brief AC apparent power phase B
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHB(40094, 1, Register::Type::INT16);

/**
 * @brief AC apparent power phase C
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHC(40095, 1, Register::Type::INT16);

/** @brief Apparent power scale factor */
constexpr Register VA_SF(40096, 1, Register::Type::INT16);

/**
 * @brief Total AC reactive power
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VAR(40097, 1, Register::Type::INT16);

/**
 * @brief AC reactive power phase A
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHA(40098, 1, Register::Type::INT16);

/**
 * @brief AC reactive power phase B
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHB(40099, 1, Register::Type::INT16);

/**
 * @brief AC reactive power phase C
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHC(40100, 1, Register::Type::INT16);

/** @brief Reactive power scale factor */
constexpr Register VAR_SF(40101, 1, Register::Type::INT16);

/**
 * @brief Total power factor
 * @unit Percent [%]
 */
constexpr Register PF(40102, 1, Register::Type::INT16);

/**
 * @brief Power factor phase A
 * @unit Percent [%]
 */
constexpr Register PFPHA(40103, 1, Register::Type::INT16);

/**
 * @brief Power factor phase B
 * @unit Percent [%]
 */
constexpr Register PFPHB(40104, 1, Register::Type::INT16);

/**
 * @brief Power factor phase C
 * @unit Percent [%]
 */
constexpr Register PFPHC(40105, 1, Register::Type::INT16);

/** @brief Power factor scale factor */
constexpr Register PF_SF(40106, 1, Register::Type::INT16);

/**
 * @brief Total energy exported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXP(40107, 2, Register::Type::UINT32);

/**
 * @brief Total energy exported phase A
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHA(40109, 2, Register::Type::UINT32);

/**
 * @brief Total energy exported phase B
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHB(40111, 2, Register::Type::UINT32);

/**
 * @brief Total energy exported phase C
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHC(40113, 2, Register::Type::UINT32);

/**
 * @brief Total energy imported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMP(40115, 2, Register::Type::UINT32);

/**
 * @brief Total energy imported phase A
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHA(40117, 2, Register::Type::UINT32);

/**
 * @brief Total energy imported phase B
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHB(40119, 2, Register::Type::UINT32);

/**
 * @brief Total energy imported phase C
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHC(40121, 2, Register::Type::UINT32);

/** @brief Energy scale factor */
constexpr Register TOTWH_SF(40123, 1, Register::Type::INT16);

/** @brief Event flags */
constexpr Register EVT(40174, 2, Register::Type::UINT32);

} // namespace M20X

/**
 * @namespace M21X
 * @brief Contains floating-point SunSpec Meter registers for Fronius inverters.
 *
 * All measurements in this namespace are represented as 32-bit IEEE
 * floating-point values. Register addresses follow the standard SunSpec float
 * model layout.
 */
namespace M21X {

/** @brief Total number of registers in the meter model (float). */
constexpr uint16_t SIZE = 124;

/**
 * @brief
 * SunSpec meter model identifier (float).
 *
 * @return
 *  - 211: Single-phase
 *  - 212: Split-phase
 *  - 213: Three-phase
 */
constexpr Register ID(40069, 1, Register::Type::UINT16);

/**
 * @brief Length of meter model block.
 * @return Always 124
 */
constexpr Register L(40070, 1, Register::Type::UINT16);

/**
 * @brief AC total current
 * @unit Amperes [A]
 */
constexpr Register A(40071, 2, Register::Type::FLOAT);

/**
 * @brief AC current phase A
 * @unit Amperes [A]
 */
constexpr Register APHA(40073, 2, Register::Type::FLOAT);

/**
 * @brief AC current phase B
 * @unit Amperes [A]
 */
constexpr Register APHB(40075, 2, Register::Type::FLOAT);

/**
 * @brief AC current phase C
 * @unit Amperes [A]
 */
constexpr Register APHC(40077, 2, Register::Type::FLOAT);

/**
 * @brief Average AC voltage phase-to-neutral
 * @unit Volts [V]
 */
constexpr Register PHV(40079, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase A to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHA(40081, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase B to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHB(40083, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase C to neutral
 * @unit Volts [V]
 */
constexpr Register PHVPHC(40085, 2, Register::Type::FLOAT);

/**
 * @brief Average AC voltage phase-to-phase
 * @unit Volts [V]
 */
constexpr Register PPV(40087, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase AB
 * @unit Volts [V]
 */
constexpr Register PPVPHAB(40089, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase BC
 * @unit Volts [V]
 */
constexpr Register PPVPHBC(40091, 2, Register::Type::FLOAT);

/**
 * @brief AC voltage phase CA
 * @unit Volts [V]
 */
constexpr Register PPVPHCA(40093, 2, Register::Type::FLOAT);

/**
 * @brief AC frequency
 * @unit Hertz [Hz]
 */
constexpr Register FREQ(40095, 2, Register::Type::FLOAT);

/**
 * @brief Total AC active power
 * @unit Watts [W]
 */
constexpr Register W(40097, 2, Register::Type::FLOAT);

/**
 * @brief AC active power phase A
 * @unit Watts [W]
 */
constexpr Register WPHA(40099, 2, Register::Type::FLOAT);

/**
 * @brief AC active power phase B
 * @unit Watts [W]
 */
constexpr Register WPHB(40101, 2, Register::Type::FLOAT);

/**
 * @brief AC active power phase C
 * @unit Watts [W]
 */
constexpr Register WPHC(40103, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent power
 * @unit Volt-amperes [VA]
 */
constexpr Register VA(40105, 2, Register::Type::FLOAT);

/**
 * @brief AC apparent power phase A
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHA(40107, 2, Register::Type::FLOAT);

/**
 * @brief AC apparent power phase B
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHB(40109, 2, Register::Type::FLOAT);

/**
 * @brief AC apparent power phase C
 * @unit Volt-amperes [VA]
 */
constexpr Register VAPHC(40111, 2, Register::Type::FLOAT);

/**
 * @brief Total AC reactive power
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VAR(40113, 2, Register::Type::FLOAT);

/**
 * @brief AC reactive power phase A
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHA(40115, 2, Register::Type::FLOAT);

/**
 * @brief AC reactive power phase B
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHB(40117, 2, Register::Type::FLOAT);

/**
 * @brief AC reactive power phase C
 * @unit Volt-ampere reactive [VAr]
 */
constexpr Register VARPHC(40119, 2, Register::Type::FLOAT);

/**
 * @brief Total power factor
 * @unit Percent [%]
 */
constexpr Register PF(40121, 2, Register::Type::FLOAT);

/**
 * @brief Power factor phase A
 * @unit Percent [%]
 */
constexpr Register PFPHA(40123, 2, Register::Type::FLOAT);

/**
 * @brief Power factor phase B
 * @unit Percent [%]
 */
constexpr Register PFPHB(40125, 2, Register::Type::FLOAT);

/**
 * @brief Power factor phase C
 * @unit Percent [%]
 */
constexpr Register PFPHC(40127, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy exported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXP(40129, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase A exported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHA(40131, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase B exported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHB(40133, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase C exported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_EXPPHC(40135, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy imported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMP(40137, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase A imported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHA(40139, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase B imported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHB(40141, 2, Register::Type::FLOAT);

/**
 * @brief Total AC energy phase C imported
 * @unit Watt-hours [Wh]
 */
constexpr Register TOTWH_IMPPHC(40143, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy exported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_EXP(40145, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase A exported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_EXPPHA(40147, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase B exported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_EXPPHB(40149, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase C exported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_EXPPHC(40151, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy imported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_IMP(40153, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase A imported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_IMPPHA(40155, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase B imported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_IMPPHB(40157, 2, Register::Type::FLOAT);

/**
 * @brief Total AC apparent energy phase C imported
 * @unit VA-hours [VA]
 */
constexpr Register TOTVAH_IMPPHC(40159, 2, Register::Type::FLOAT);

/** @brief Event flags */
constexpr Register EVT(40193, 2, Register::Type::UINT32);

} // namespace M21X

/**
 * @namespace END
 * @brief SunSpec end-of-block registers.
 * @details
 * This namespace defines the SunSpec "end model" block that marks the end of a
 * SunSpec register map within Fronius devices. It is typically composed of two
 * registers — an identifier (`ID`) with a constant value of 0xFFFF and a length
 * field (`L`) that is always 0.
 */
namespace M_END {

/**
 * @brief Offset to convert integer and scale factor register addresses
 * to float addresses.
 *
 * @details
 * In Fronius devices, certain SunSpec float registers are implemented as
 * integer registers with associated scale factors. To obtain the integer
 * register address corresponding to a float register, add this offset to
 * the integer register address (`ADDR`).
 */
constexpr uint16_t FLOAT_OFFSET = 19;

/**
 * @brief End block identifier
 * @return Always 0xFFFF
 */
constexpr Register ID(40176, 1, Register::Type::UINT16);

/**
 * @brief End block length
 * @return Always 0
 */
constexpr Register L(40177, 1, Register::Type::UINT16);

} // namespace M_END

#endif /* METER_REGISTERS_H_ */
