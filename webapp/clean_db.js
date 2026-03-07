const NEON_URL = "https://ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/sql";
const NEON_CONN = "postgresql://neondb_owner:npg_jsaCe7Qq1oBl@ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require";

async function cleanDB() {
    try {
        console.log("Cleaning old/bad data from database...");
        // TRUNCATE table cascade removes all rows from robot_sessions and robot_frames
        const response = await fetch(NEON_URL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'neon-connection-string': NEON_CONN },
            body: JSON.stringify({ query: 'TRUNCATE TABLE robot_sessions CASCADE;' })
        });

        if (!response.ok) {
            console.error("Failed to clean database:", await response.text());
        } else {
            console.log("Success! Database has been cleared of all old data.");
        }
    } catch (e) {
        console.error("Error:", e);
    }
}
cleanDB();
