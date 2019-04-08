# Led Gate Module for Chorus Laptimer

This is an add-in hardware for the Chorus Laptimer device.

It allows you to light up the moment when a racing drone passes the finish gate. Each VTX channel has its own color, so spectators can easily see which pilot makes the lap, like this:

[![Chorus LED Gate Video](https://img.youtube.com/vi/h5rXzPiw1T4/0.jpg)](https://youtu.be/h5rXzPiw1T4)

## How it works

The module listens to the UART data traffic from Chorus nodes and therefore "knows" when a heat is started/finished and when any node detects a new lap. So whenever a new lap is detected in the race mode, the LEDs are lit with a color corresponding to the number of the Chorus node which reported the lap.

The module introduces its own extension for Chorus API (commands starting with "T").

## Attachment to Chorus

The module doesn't issue commands, just listens to them. This makes it possible to connect it in parallel with Chorus UART Tx line.
Schematically it should look like this:

```
Chorus nodes:N1 -> N2 -> ... Nn +-->  Bluetooth/WiFi Dongle
                                | 
                                | (2 wires: Gnd & Tx)
                                |
                                +--> LED Gate module -> LED strips
```

# Flashing:

Flash first arduino with `chorus_led_module.ino`

Flash subsequent modules with `WS2812-driver.ino`

# Commands for Chorus to drive the Led Module:

## Generic:

| Description          | Command          | Example |
|----------------------|------------------|------|
| Prepare to countdown | `TP`             | none |
| Countdown 3          | `T3`             | none |
| Countdown 2          | `T2`             | none |
| Countdown 1          | `T1`             | none |
| Start race           | `R*R1` (or `R*R2)` | none |
| End race             | `R*R0`           | none |
| Lap Racer #0         | `S0Lxyyyyyyyy`   | none |
| Lap Racer #1         | `S1Lxyyyyyyyy`   | none |
| Lap Racer #2         | `S2Lxyyyyyyyy`   | none |
| Lap Racer #3         | `S3Lxyyyyyyyy`   | none |
| ...                  |  ...             | none |
| Lap Racer #7         | `S7Lxyyyyyyyy`   | none |
| Start calibration    | `R*H1` (or `S<module>H1`) | none |
| Calibration stage 2  | `S<module>H2`    | none |
| Calibration end      | `S<module>H0`    | none |
| Set modules count (for calibration and showing color assignments) | `TN<modules_count>` | none |


## Setting colors:

These commands store the selected matching of colors to Chorus nodes in Arduino EEPROMs, so once set - the colors are saved for subsequent usage.

Possible Color indices:

- 0 - Red
- 1 - Yellow
- 2 - Green
- 3 - Blue
- 4 - Orange
- 5 - Aqua
- 6 - Purple
- 7 - Pink

| Description      | Command            | Example                   |
|------------------|--------------------|---------------------------|
| Show color assignments | `TS`    |  |
| Set Color for #0 | `TC0<color_index>` | `TC03` - blue on node #0  |
| Set Color for #1 | `TC1<color_index>` | `TC12` - green on node #1 |
| Set Color for #2 | `TC2<color_index>` | `TC21` - yellow on node # |
| Set Color for #3 | `TC3<color_index>` | `TC30` - red on node #3   |
| ... | ... | ... |
| Set Color for #7 | `TC7<color_index>` | `TC75` - aqua on node #7  |
