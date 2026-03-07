import { neon } from '@neondatabase/serverless';

const str = 'postgresql://neondb_owner:npg_jsaCe7Qq1oBl@ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require';

async function testBrowser() {
    try {
        const res = await fetch(`https://ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/sql`, {
            method: 'POST',
            headers: { 'neon-connection-string': str, 'Content-Type': 'application/json' },
            body: JSON.stringify({ query: 'SELECT 1;' })
        });
        console.log("Status:", res.status);
        if (!res.ok) {
            console.error("Text:", await res.text());
        } else {
            console.log("Success");
        }
    } catch (e) {
        console.error("EX:", e.message);
    }
}

testBrowser();
