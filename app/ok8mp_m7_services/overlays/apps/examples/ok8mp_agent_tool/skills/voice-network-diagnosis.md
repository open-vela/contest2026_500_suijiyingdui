# Voice And Network Diagnosis

Use this skill when a spoken hardware command does not produce the expected
result or when an online Agent request fails.

## Procedure

1. Call `voice_status` and verify that the module is idle and has five words.
2. Call `wait_voice_command` while the user repeats the command.
3. Verify Ethernet with `ifconfig`, `route ipv4`, and `ping`.
4. Run `https_client probe` to separate DNS/TCP failures from TLS failures.
5. Run `https_client get` and require a verified certificate before calling
   an LLM API.

Never report an LLM call as successful unless an HTTP 2xx response is logged.
