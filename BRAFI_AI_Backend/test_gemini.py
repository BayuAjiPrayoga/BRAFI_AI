import asyncio
import aiohttp

GEMINI_API_KEY = "YOUR_GEMINI_API_KEY_HERE"
GEMINI_URL = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key={GEMINI_API_KEY}"

async def main():
    payload = {
        "system_instruction": {"parts": [{"text": "You are a helpful assistant."}]},
        "contents": [{"parts": [{"text": "Hello!"}]}]
    }
    headers = {"Content-Type": "application/json"}
    async with aiohttp.ClientSession() as session:
        async with session.post(GEMINI_URL, json=payload, headers=headers) as resp:
            print(resp.status)
            print(await resp.text())

asyncio.run(main())
