This project is to create ant weight battle bots that use the following components and/or constraints:

- ESP32 based
- N20 motors
- Wi-Fi and/or BLE based connectivity
- 150g max (5.3oz)
- Must fit within a 10cm (4 inch) cube
- A fail-safe is required (bot must stop if signal
- No liquid-based weapons, glue, adhesives, or explosives.
- Generally, no nets, fabrics, or entangling weapons.
- All sharp edges must be covered when outside the arena.
- Robots must be constructed primarily from 3D printed parts
-   Only PLA is allowed as a material (except TPU 85/90 for wheel covers if not using o-rings for grip)

----------------------------------------------------------------------------------------------
Components used for prototyping include

- https://www.amazon.com/dp/B0CR2RH7PS?ref=ppx_yo2ov_dt_b_fed_asin_title (ESP32-S3 mini)
- https://www.amazon.com/dp/B0F8NH1M4Z?ref=ppx_yo2ov_dt_b_fed_asin_title (N20 motors)
- https://www.amazon.com/dp/B0DB8CX8LK?ref=ppx_yo2ov_dt_b_fed_asin_title (DRV8833 (Dual H Bridge)
  
----------------------------------------------------------------------------------------------
Tips gleaned from forums, sites, and AI queries

- Motor Direction — If a motor spins backwards, swap its two motor wires (M1/M2) on the DRV8833 outputs.
- Encoder Direction — If counts go negative when moving forward, swap C1 and C2 on that motor.
- Sleep Pin on DRV8833 (if present) — Tie to 3.3V or GPIO so the driver stays awake.
- Current — N20s draw little; DRV8833 handles it easily. Add a small capacitor (100–470µF) across VM/GND for stability.

----------------------------------------------------------------------------------------------
ESP32-S3 Super Mini: Use the pin labels from sketch (GPIO 6, 8, 15, 16, 17, 18, 19, 21, 22, 3.3V, GND).

Key Wiring:

Motor Power (Thick Red/Black):
Battery + → DRV8833 VM
Battery – → DRV8833 GND + ESP32 GND (common ground)
Left N20 Motor (Channel A):
Motor wires (M1/M2) → DRV8833 AOUT1 & AOUT2 (green/yellow)
Encoder VCC → ESP32 3.3V (red)
Encoder GND → ESP32 GND (black)
C1 → GPIO 18 (blue)
C2 → GPIO 21 (orange)

Right N20 Motor (Channel B):
Motor wires → DRV8833 BOUT1 & BOUT2
Encoder VCC → ESP32 3.3V
Encoder GND → ESP32 GND
C1 → GPIO 19 (blue)
C2 → GPIO 22 (orange)

Control Signals:
L_IN1 (AIN1) → GPIO 16 (purple)
L_IN2 (AIN2) → GPIO 17 (purple)
R_IN1 (BIN1) → GPIO 15 (purple)
R_IN2 (BIN2) → GPIO 8 (purple)

Weapon Servo:
Signal → GPIO 6 (yellow)
VCC → 5V BEC/regulator (red)
GND → Common GND (black)


Power Recommendations (Critical for Battlebots)

Motors: Direct from your LiPo (6–8.4V) into DRV8833 VM.
ESP32: USB for testing, or a 5V BEC in the bot.
Add a 100–470 µF capacitor across VM/GND on the DRV8833.
