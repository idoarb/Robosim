
const url = "https://ep-muddy-fire-ads9qx8f-pooler.c-2.us-east-1.aws.neon.tech/sql";
const key = "npg_o4Qqw5SsyHFM";

async function test() {
    const query = "SELECT 1";
    try {
        const res = await fetch(url, {
            method: 'POST',
            headers: { 'Authorization': `Bearer ${key}`, 'Content-Type': 'application/json' },
            body: JSON.stringify({ query })
        });
        console.log("Status:", res.status);
        const text = await res.text();
        console.log("Response:", text);
    } catch (e) {
        console.error("Fetch Error:", e);
    }
}

test();
