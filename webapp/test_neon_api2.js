const API_KEY = "napi_t9kolaci3y1on5mmaan8zg32krujd2sil9h3wc96t6w7kcqf4i0y1jhk9go3j8yu";
const PROJECT_ID = "young-butterfly-36686809";

async function fetchNeonData() {
    try {
        const branchRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/branches`, {
            headers: { 'Authorization': `Bearer ${API_KEY}`, 'Accept': 'application/json' }
        });
        const branchData = await branchRes.json();
        const branchId = branchData.branches[0].id;
        console.log("Branch ID:", branchId);

        const endpointsRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/endpoints`, {
            headers: { 'Authorization': `Bearer ${API_KEY}`, 'Accept': 'application/json' }
        });
        const endpointsData = await endpointsRes.json();
        const endpointId = endpointsData.endpoints[0].id;
        console.log("Endpoint Host:", endpointsData.endpoints[0].host);

        const rolesRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/branches/${branchId}/roles`, {
            headers: { 'Authorization': `Bearer ${API_KEY}`, 'Accept': 'application/json' }
        });
        const rolesData = await rolesRes.json();
        console.log("Roles List:");
        rolesData.roles.forEach(r => console.log(r.name));

        const databasesRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/branches/${branchId}/databases`, {
            headers: { 'Authorization': `Bearer ${API_KEY}`, 'Accept': 'application/json' }
        });
        const databasesData = await databasesRes.json();
        console.log("Databases List:");
        databasesData.databases.forEach(d => console.log(d.name));

    } catch (e) {
        console.error("Error:", e);
    }
}

fetchNeonData();
