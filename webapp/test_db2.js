const NEON_URL = "https://ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/sql";
const NEON_CONN = "postgresql://neondb_owner:npg_jsaCe7Qq1oBl@ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require";

async function queryDB() {
    try {
        const response = await fetch(NEON_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'neon-connection-string': NEON_CONN },
            body: JSON.stringify({ query: 'SELECT * FROM robot_frames ORDER BY id DESC LIMIT 10;' })
        });
        const data = await response.json();
        console.log("Response OK:", response.ok);
        for (let row of data.rows) {
            console.log(row.created_at, row.session_id, row.frame_index);
        }
    } catch (e) {
        console.error("Error:", e);
    }
}
queryDB();
