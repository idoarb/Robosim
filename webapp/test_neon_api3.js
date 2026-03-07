const API_KEY = "napi_t9kolaci3y1on5mmaan8zg32krujd2sil9h3wc96t6w7kcqf4i0y1jhk9go3j8yu";
const PROJECT_ID = "young-butterfly-36686809";
const BRANCH_ID = "br-rapid-meadow-adkqofco";
const ROLE_NAME = "neondb_owner";

async function fetchNeonData() {
    try {
        const passRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/branches/${BRANCH_ID}/roles/${ROLE_NAME}/reset_password`, {
            method: 'POST',
            headers: { 'Authorization': `Bearer ${API_KEY}`, 'Accept': 'application/json' }
        });
        const passData = await passRes.json();
        console.log("New Password:", passData.role.password);

    } catch (e) {
        console.error("Error:", e);
    }
}

fetchNeonData();
