import { neon } from '@neondatabase/serverless';

const sql2 = neon('postgresql://neondb_owner:npg_o4Qqw5SsyHFM@ep-young-butterfly-36686809-pooler.us-east-1.aws.neon.tech/neondb?sslmode=require');
const sql3 = neon('postgresql://neondb_owner:npg_o4Qqw5SsyHFM@ep-young-butterfly-36686809.us-east-2.aws.neon.tech/neondb?sslmode=require');

async function test(sql, name) {
    try {
        const result = await sql`SELECT 1 as num`;
        console.log(name, "Success:", result);
    } catch (e) {
        console.error(name, "Error:", e.message);
    }
}

async function run() {
    await test(sql2, "sql2");
    await test(sql3, "sql3");
}

run();
