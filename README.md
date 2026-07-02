NOTE: THIS PROJECT IS A WORK IN PROGRESS AS WE BUILD THESE IN REAL TIME WITH OUR YOUTH GROUP. 

NOTE: They have little coding experience, so code is heavily commented which may just be noise to you

NOTE: THAT MEANS THINGS ARE CHANGING AND THINGS WILL CHANGE

NOTE: USE AT YOUR OWN RISK

This project is to create ant weight battle bots that use the following components and/or constraints:

- ESP32 based (All wiring, etc. in this sketch is for ESP32-S3-WROOM-1, but will be testing with ESP32-S3-Zero (mini) )
- N20 motors (with or without encoders, wiring diagrams below for either) -> 2 powered wheels for motion, but 4 wheels or tracks allowed with the 2 drives.
- Wi-Fi and/or BLE based connectivity
- 150g max (5.3oz)
- Must fit within a 10cm (4 inch) cube
- A fail-safe is required (bot must stop if signal is lost in a minimal amount of time)
- No liquid-based weapons, glue, adhesives, or explosives.
- Generally, no nets, fabrics, or entangling weapons.
- All sharp edges must be covered when outside the arena.
- Robots must be constructed primarily from 3D printed parts
-   Only PLA is allowed as a material (except TPU 85/90 for wheel covers if not using o-rings for grip)

----------------------------------------------------------------------------------------------
Components used for prototyping include

- https://www.amazon.com/dp/B0CR2RH7PS (ESP32-S3 mini)
- https://www.amazon.com/dp/B0F8NH1M4Z (N20 motors - 6 V 100 rpm)
- https://www.amazon.com/dp/B0DB8CX8LK (DRV8833 (Dual H Bridge)
- https://www.amazon.com/gp/product/B0D3F5YF9L (7.2 V battery)
- https://www.amazon.com/gp/product/B089GV88DK (MP1584en 3V regulator for ESP)
- https://www.amazon.com/gp/product/B09R43HCY3/ (power switch for safety shutoff)
  
----------------------------------------------------------------------------------------------
Tips gleaned from forums, sites, and AI queries

- Motor Direction — If a motor spins backwards, swap its two motor wires (M1/M2) on the DRV8833 outputs.
- Encoder Direction — If counts go negative when moving forward, swap C1 and C2 on that motor.
- Sleep Pin on DRV8833 (if present) — Tie to 3.3V or GPIO so the driver stays awake.
- Current — N20s draw little; DRV8833 handles it easily. Add a small capacitor (100–470µF) across VM/GND for stability.

----------------------------------------------------------------------------------------------

<img width="1440" height="764" alt="image" src="https://github.com/user-attachments/assets/53ddb07d-1f58-4ff0-9578-2dfff5f7427f" />

