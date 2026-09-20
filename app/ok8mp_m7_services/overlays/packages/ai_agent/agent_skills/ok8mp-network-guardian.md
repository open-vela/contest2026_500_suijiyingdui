<!-- SPDX-License-Identifier: Apache-2.0 -->

# OK8MP Network Guardian

Diagnose the OK8MP Ethernet-to-Internet path and show the result using the
voice module RGB LED and buzzer.

## When to use

Use this skill when an online Agent request fails, when the user asks whether
the board can access the Internet, or when HTTPS/TLS connectivity must be
verified on the real board.

## How to use

1. Call `voice_status` to prove that the hardware module is reachable.
2. Call `set_voice_rgb` with blue `(0,0,255)` to show that diagnosis is running.
3. Call `network_health_check`. Treat `ok=true`, `dns=true`,
   `tls_verified=true`, and an HTTP status in `200..299` as the only complete
   success result. This is a structured C Tool, not shell-output guessing.
4. For an expanded diagnostic requested by the user, additionally call
   `run_shell` with `ping -c 3 192.168.2.1` and `nslookup example.com`.
5. If every required step passes, call `set_voice_rgb` with green `(0,255,0)`.
   If any
   step fails, call it with red `(255,0,0)` and call `set_voice_buzzer` with
   `enabled=true`; include the first failed layer in the reply.

## Result rules

- DNS/TCP reachability alone is not HTTPS success.
- Never report TLS success unless `network_health_check` returns
  `tls_verified=true`.
- Never report end-to-end success unless the HTTP response is 2xx.
- Always use at least `voice_status`, `set_voice_rgb`, and
  `network_health_check` so the demonstration contains three distinct Tool
  types and a visible hardware result.

## Example

User: “检查开发板能否安全访问外网，并用灯告诉我结果。”

Run the hardware status, blue progress light, structured network health check,
and final green/red light in order, then summarize the DNS, X.509 and HTTP
evidence.
