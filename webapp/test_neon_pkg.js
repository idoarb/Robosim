import { neon } from '@neondatabase/serverless';

const sql = neon('postgresql://neondb_owner:npg_o4Qqw5SsyHFM@ep-muddy-fire-ads9qx8f.us-east-1.aws.neon.tech/neondb?sslmode=require');

async function test() {
    try {
        const result = await sql`SELECT 1 as num`;
        console.log("Success:", result);
    } catch (e) {
        console.error("Error:", e);
    }
}

test();
