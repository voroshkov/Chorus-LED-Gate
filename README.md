Flash first arduino with "chorus_led_module.ino"
Flash subsequent modules with "WS2812-driver.ino"

# Commands for Chorus to drive the Led Module:

## Generic:
| Description          | Command          |
|----------------------|------------------|
| Prepare to countdown | `TP`             |
| Countdown 3          | `T3`             |
| Countdown 2          | `T2`             |
| Countdown 1          | `T1`             |
| Start race           | `R*R1 (or R*R2)` |
| End race             | `R*R0`           |
| Lap Racer #0         | `S0Lxyyyyyyyy`   |
| Lap Racer #1         | `S1Lxyyyyyyyy`   |
| Lap Racer #2         | `S2Lxyyyyyyyy`   |
| Lap Racer #3         | `S3Lxyyyyyyyy`   |


## Setting colors:

Possible Color indices:
* 0 - Red
* 1 - Yellow
* 2 - Green
* 3 - Blue

- Set Color for #0        `TC0<color_index>` (e.g. `TC03` - blue on node #0)
- Set Color for #1        `TC1<color_index>` (e.g. `TC12` - green on node #1)
- Set Color for #2        `TC2<color_index>` (e.g. `TC21` - yellow on node #)
- Set Color for #3        `TC3<color_index>` (e.g. `TC30` - red on node #3)