# ProsperoEden - Runs RomM's library scan as its web interface does, for tools/check-romm.py.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""scan.py <user> <password>, inside the RomM container (it has socketio and aiohttp)

Signs in, asks for a scan over RomM's socket (/ws: "scan", as the web interface does; the scan task
cannot be started over the REST API) and waits until it says it is done. Prints what it found.
"""
import asyncio, base64, sys
import aiohttp, socketio

async def main(user, password):
    jar = aiohttp.CookieJar(unsafe=True)
    async with aiohttp.ClientSession(cookie_jar=jar) as http:
        auth = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
        async with http.post("http://127.0.0.1:8080/api/login", headers={"Authorization": auth}) as answer:
            print("login", answer.status)
        cookies = "; ".join(f"{c.key}={c.value}" for c in jar)
        client = socketio.AsyncClient()
        done = asyncio.Event()
        result = {}
        @client.on("scan:done")
        async def ok(data=None):
            result["ok"] = data; done.set()
        @client.on("scan:done_ko")
        async def ko(data=None):
            result["ko"] = data; done.set()
        await client.connect("http://127.0.0.1:8080", socketio_path="/ws/socket.io", headers={"Cookie": cookies},
                             transports=["websocket"])
        await client.emit("scan", {"platforms": [], "type": "quick", "apis": []})
        try:
            await asyncio.wait_for(done.wait(), 300)
        except asyncio.TimeoutError:
            result["timeout"] = True
        print("scan", result)
        await client.disconnect()

asyncio.run(main(sys.argv[1], sys.argv[2]))
