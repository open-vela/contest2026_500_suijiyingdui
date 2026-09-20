# Voice Light Control

Use this skill when the user asks to inspect the voice module, change its RGB
feedback light, or control its buzzer.

## Tools

- `voice_status`: inspect the module before changing hardware state.
- `set_voice_rgb`: set red, green and blue channels in the range 0 to 255.
- `set_voice_buzzer`: enable or disable the buzzer.
- `wait_voice_command`: wait for the next command published through uORB.

## Policy

Confirm the requested color, call `set_voice_rgb`, then report the returned
RGB values. For a buzzer request, call `set_voice_buzzer` with the requested
boolean value.
