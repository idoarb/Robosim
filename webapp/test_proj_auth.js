import { neon } from '@neondatabase/serverless';

const sql = neon('postgresql://neondb_owner:npg_jsaCe7Qq1oBl@ep-muddy-fire-ads9qx8f-pooler.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require');

async function test(sql, name) {
    try {
        const result = await sql`SELECT 1 as num`;
        console.log(name, "Success:", result);
    } catch (e) {
        console.error(name, "Error:", e.message);
    }
}

async function run() {
    await test(sql, "test connection");
}

run();
