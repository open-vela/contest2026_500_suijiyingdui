<!-- SPDX-License-Identifier: Apache-2.0 -->

# OK8MP Voice Light Control

Control and inspect the RGB LED and buzzer on the OK8MP offline voice module.

## When to use

Use this skill when the user asks to inspect the voice module, change its
feedback color, acknowledge a spoken command, or enable/disable its buzzer.

## How to use

1. Call `voice_status` before changing hardware and report the module version,
   busy state, word count, and latest command ID.
2. For a color request, convert the color to red/green/blue values in 0..255,
   then call `set_voice_rgb` with all three channels.
3. For a buzzer request, call `set_voice_buzzer` with `enabled=true` or
   `enabled=false` exactly as requested.
4. Read the returned JSON and confirm the actual hardware state. Never claim
   success when the tool returns `ok=false`.

## Example

User: “检查语音模块，然后把指示灯设为紫色。”

1. `voice_status`
2. `set_voice_rgb` with `{"red":128,"green":0,"blue":128}`
3. Reply with the module status and the applied RGB values.
