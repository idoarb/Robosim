const API_KEY = "napi_t9kolaci3y1on5mmaan8zg32krujd2sil9h3wc96t6w7kcqf4i0y1jhk9go3j8yu";
const PROJECT_ID = "young-butterfly-36686809";

async function fetchNeonData() {
    try {
        const response = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/branches`, {
            headers: {
                'Authorization': `Bearer ${API_KEY}`,
                'Accept': 'application/json'
            }
        });

        if (!response.ok) {
            console.error("Failed to fetch branches:", await response.text());
            return;
        }

        const data = await response.json();
        const branchId = data.branches[0].id;
        console.log("Found Branch ID:", branchId);

        const endpointsRes = await fetch(`https://console.neon.tech/api/v2/projects/${PROJECT_ID}/endpoints`, {
            headers: {
                'Authorization': `Bearer ${API_KEY}`,
                'Accept': 'application/json'
            }
        });

        const endpointsData = await endpointsRes.json();
        const endpointId = endpointsData.endpoints[0].id;
        console.log("Found Endpoint ID:", endpointId);

    } catch (e) {
        console.error("Error:", e);
    }
}

fetchNeonData();
