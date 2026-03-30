Local FFT/Fourier receiver (laptop)

Run:
  python tools/fft_server.py --port 8080

Health check:
  curl http://127.0.0.1:8080/health

Send example JSON:
  curl -X POST http://127.0.0.1:8080/fft ^
    -H "Content-Type: application/json" ^
    -d "{\"sample_rate_hz\":16000,\"n\":1024,\"dc\":0.01,\"rms\":0.23,\"peak\":{\"hz\":1000.0,\"mag\":123.4}}"

Notes:
  - Make sure your laptop firewall allows inbound TCP on the chosen port.
  - ESP32 and laptop must be on the same network (or routed) to reach the server.
  - If you want to save every payload to disk:
      python tools/fft_server.py --out fft_log.jsonl
