# TVBridge — TradingView → OMS Order Bridge

**High-performance C++ webhook gateway** that turns TradingView (or any alert source) signals into real exchange orders on your OMS / CMS trading stack.

Built for brokers, prop desks, and algo teams who want **chart alerts → live orders** without a slow middleware layer.

---

## Why this product exists

Most trading desks already use **TradingView** for signals and a separate **OMS/CMS** for execution. Connecting them usually means:

| Pain | Without TVBridge | With TVBridge |
|------|----------------------|-------------------|
| Latency | Node/Python glue + polling | Native C++ HTTP + binary TCP to CMS |
| Reliability | Ad-hoc scripts break on deploy | Config-driven daemon, health checks, rotating logs |
| Symbol mapping | Manual / wrong tickers | DB-backed contract resolution (NSE / BSE) |
| Strategy routing | Hard-coded IDs in alerts | Strategy config looked up by `strategy_name` |
| Security | Open webhooks | Token-gated `?token=` auth |
| Ops visibility | Scattered prints | Structured logs + dedicated order-response log |

**If you sell or run automated strategies from TradingView into a professional OMS, you need a purpose-built bridge — not a weekend script.**

---

## What it does

```
TradingView alert (JSON)
        │  POST /api/v1/webhook?token=***
        ▼
   TVBridge (libmicrohttpd)
        │  validate → resolve contract → load strategy qty/client
        ▼
   CMS / OMS (binary TCP packet)
        │
        ▼
   Exchange order path + async fill/update logging
```

1. Accepts **TradingView webhook** JSON (buy/sell, symbol, price, exchange, strategy…).
2. Authenticates with a shared **token**.
3. Resolves **contract IDs** from Postgres security-master (NSE + optional BSE).
4. Loads **quantity / client / strategy** from DB by `strategy_name`.
5. Packs a **binary CMS order message** and sends it over TCP.
6. Background loop logs **order updates & fills** from CMS.

### HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/health` | Liveness JSON (timestamp) |
| `GET` | `/` | Plain status line |
| `POST` | `/api/v1/webhook?token=<auth.token>` | Ingest TradingView (or compatible) alert |

#### Example webhook body

```json
{
  "symbol": "NIFTY",
  "action": "BUY",
  "price": 24500.5,
  "exchange": "NSE",
  "timestamp": "2026-07-21T10:00:00Z",
  "strategy_name": "orb_breakout",
  "order_type": "limit"
}
```

**Required fields:** `symbol`, `action` (`BUY`/`SELL`), `price` (> 0), `exchange`, `timestamp`, `strategy_name`, `order_type` (`limit`/`market`).

TradingView alert message can use `{{strategy.order.action}}` etc. and POST JSON to your public URL (or tunnel).

---

## Who should buy / use this

- **Prop / algo desks** wiring TV strategies into an in-house OMS  
- **Brokers** offering “alert-to-order” for dealers  
- **Vendors** packaging a TradingView connector for a CMS product line  
- Teams who need **C++ performance** and **binary protocol** fidelity (not REST fan-out)

---

## Tech stack

- **C++17**, CMake  
- **libmicrohttpd** — embeddable HTTP server  
- **kissnet** — TCP client to CMS  
- **PostgreSQL** via **SQLAPI++** (runtime licensed separately)  
- Config: `config.ini`  
- Dual DB: NSE + optional BSE  

---

## Quick start

### Dependencies (Linux)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libmicrohttpd-dev
```

Install **SQLAPI++** from [sqlapi.com](https://www.sqlapi.com/) and place libraries under:

`databaseManager/SQLAPI_new/lib/` (e.g. `libsqlapi.so`)

CMS / OMS shared headers must be available at the paths referenced in `CMakeLists.txt` (or adjust those include paths for your tree).

### Configure

```bash
cp config.example.ini config.ini
# edit auth.token, server, cms, database*
```

### Build & run

```bash
cmake -S . -B build
cmake --build build -j
./build/webserver ./config.ini
# or: bash run.sh
```

Verify:

```bash
curl -s http://127.0.0.1:8080/health
curl -s -X POST "http://127.0.0.1:8080/api/v1/webhook?token=YOUR_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"symbol":"NIFTY","action":"BUY","price":24500.5,"exchange":"NSE","timestamp":"2026-07-21T10:00:00Z","strategy_name":"orb_breakout","order_type":"limit"}'
```

---

## Deployment notes

- Bind `server.ip` / `server.port` behind a reverse proxy (nginx/Caddy) with TLS for internet-facing TradingView alerts.  
- Keep `auth.token` long and rotated; never commit `config.ini`.  
- Ensure CMS TCP host is reachable from the webserver host.  
- NSE **or** BSE DB must initialize; both preferred for multi-exchange.  
- Logs: `logs/log.log` + `logs/order_responses.log`.

---

## Live demo?

**Not hosted publicly.** This product talks to a private OMS, Postgres security-master, and dealer credentials. A public SaaS demo would expose trading infrastructure.

For evaluation: run against a staging CMS + DB, hit `/health` and `/api/v1/webhook` with test tokens.

**No separate web UI is required** — TradingView is the frontend; this service is the execution bridge. Ops use `/health` + logs.

---

## Repository layout

```
TVBridge/
├── src/                 # HTTP, webhook, TCP, JSON, logging
├── include/             # Public headers + CMS packet layouts
├── databaseManager/   # Postgres access layer
├── config.example.ini
├── CMakeLists.txt
└── README.md
```

---

## License & commercial use

Source in this repository is provided by **Gagan Yadav** for productization / licensing discussions.

- **SQLAPI++** remains under its own [vendor license](https://www.sqlapi.com/) — obtain a proper license before redistributing binaries.  
- OMS/CMS protocols and dealer infrastructure remain your (or your client’s) responsibility.

**Interested in a commercial license, white-label, or integration support?**  
Contact: [gaganyadav424266@gmail.com](mailto:gaganyadav424266@gmail.com) · [GitHub](https://github.com/Gagan424266)

---

## Author

**Gagan Yadav** — Full-stack & low-latency systems (C++, trading, React).  
Portfolio: [gagan-yadav.pages.dev](https://gagan-yadav.pages.dev)
