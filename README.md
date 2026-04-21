This project is to create ant weight battle bots that use the following components and/or constraints:

NOTE: THIS PROJECT IS A WORK IN PROGRESS AS WE BUILD THESE IN REAL TIME WITH OUR YOUTH GROUP. 
NOTE: THAT MEANS THIS ARE CHANGING AND THINGS WILL CHANGE
NOTE: USE AT YOUR OWN RISK

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

