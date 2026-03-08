import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
// @ts-ignore
import loadMujoco from 'mujoco-js';
import { neon } from '@neondatabase/serverless';

// Connection URL and Password fetched dynamically securely
const NEON_CONN = "postgresql://neondb_owner:npg_jsaCe7Qq1oBl@ep-muddy-fire-ads9qx8f.c-2.us-east-1.aws.neon.tech/neondb?sslmode=require";
const sqlClient = neon(NEON_CONN);
const SESSION_ID = "web_" + Math.random().toString(36).substring(2, 6) + "_" + Date.now().toString(36).substring(6);

// --- State ---
let mujoco: any;
let model: any;
let state: any;
let simulation: any;
let scene: THREE.Scene;
let camera: THREE.PerspectiveCamera;
let renderer: THREE.WebGLRenderer;
let controls: OrbitControls;
let robotMeshes: THREE.Group = new THREE.Group();
let framesSent = 0;
let frameIdx = 0;
let isInitialized = false;
let dbReady = false;
const frameBuffer: string[] = [];

// --- Keyboard Controls ---
const keys: Record<string, boolean> = {};
window.addEventListener('keydown', (e) => {
    keys[e.code] = true;
    console.log("KeyDown: ", e.code);
});
window.addEventListener('keyup', (e) => {
    keys[e.code] = false;
});

const setStatus = (msg: string) => {
    const el = document.getElementById('loading-status');
    if (el) el.innerText = msg;
    console.log("[SIM]", msg);
};

// --- Initialize Simulation ---
async function init() {
    setStatus("Connecting to MuJoCo Wasm...");

    // 1. MuJoCo Loader
    try {
        setStatus("Initializing MuJoCo Wasm...");
        // @ts-ignore
        mujoco = await loadMujoco();
        setStatus("MuJoCo Wasm Loaded");
    } catch (e) {
        setStatus("MuJoCo Load Error: " + e);
        return;
    }

    // Load model
    try {
        setStatus("Setting up model...");
        const sphereModel = `
<mujoco model="SphereBot">
  <worldbody>
    <light pos="0 0 3"/>
    <geom type="plane" size="10 10 0.1" rgba=".2 .2 .2 1"/>
    <body name="robot" pos="0 0 0.5">
      <freejoint name="root"/>
      <geom type="sphere" size="0.2" rgba="0 0.8 1 1"/>
    </body>
  </worldbody>
</mujoco>`;
        const FS = (mujoco as any).FS;
        if (FS) {
            FS.writeFile('/working_model.xml', sphereModel);
            model = mujoco.MjModel.loadFromXML('/working_model.xml');
            state = new mujoco.MjData(model);
        } else {
            console.error("FS is not available on mujoco module!");
        }

        console.log("[SIM] Sphere model loaded successfully");
    } catch (e) {
        console.error("[SIM] Model load failed", e);
        setStatus("Model Load Error");
        return;
    }

    // 2. Three.js Setup
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0c10);

    const canvas = document.getElementById('main-canvas') as HTMLCanvasElement;
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.shadowMap.enabled = true;

    camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.1, 100);
    camera.position.set(4, 3, 4);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(0, 1, 0);
    controls.update();

    // Lights
    const ambient = new THREE.AmbientLight(0xffffff, 0.5);
    scene.add(ambient);

    const spot = new THREE.SpotLight(0x00ffff, 100);
    spot.position.set(5, 10, 5);
    spot.angle = 0.5;
    spot.penumbra = 0.5;
    spot.castShadow = true;
    scene.add(spot);

    // Grid & Floor
    const grid = new THREE.GridHelper(20, 20, 0x00ffff, 0x1a1c22);
    scene.add(grid);

    const floorGeo = new THREE.PlaneGeometry(30, 30);
    const floorMat = new THREE.MeshPhongMaterial({ color: 0x111318, side: THREE.DoubleSide });
    const floor = new THREE.Mesh(floorGeo, floorMat);
    floor.rotation.x = Math.PI / 2;
    floor.position.y = -0.01;
    scene.add(floor);

    setStatus("Setting up environment...");
    // Warehouse Environment
    createEnvironment();

    // Robot
    robotMeshes.rotation.x = -Math.PI / 2;
    scene.add(robotMeshes);
    createRobotVisuals();

    // 3. NeonDB Init
    setStatus("Syncing with NeonDB...");
    initSession();

    window.addEventListener('resize', onWindowResize);

    isInitialized = true;
    const loader = document.getElementById('loading');
    if (loader) loader.style.display = 'none';

    requestAnimationFrame(loop);
}

function createEnvironment() {
    const boxGeo = new THREE.BoxGeometry(1, 1, 1);
    const boxMat = new THREE.MeshPhongMaterial({ color: 0x22242b });

    // Back wall
    const wall = new THREE.Mesh(new THREE.BoxGeometry(20, 8, 0.5), boxMat);
    wall.position.set(0, 4, -10);
    scene.add(wall);

    // Scattered crates
    for (let i = 0; i < 5; i++) {
        const crate = new THREE.Mesh(boxGeo, new THREE.MeshPhongMaterial({ color: 0x8b5a2b }));
        crate.position.set(-6 + i * 3, 0.5, -4);
        crate.castShadow = true;
        scene.add(crate);
    }
}

function createRobotVisuals() {
    robotMeshes.clear();
    console.log("[SIM] Creating visuals for", model.ngeom, "geoms");
    for (let i = 0; i < model.ngeom; i++) {
        const type = model.geom_type[i];
        const size = model.geom_size.subarray(i * 3, i * 3 + 3);
        let geo;

        switch (type) {
            case 2: geo = new THREE.SphereGeometry(size[0]); break;
            case 3: geo = new THREE.CapsuleGeometry(size[0], size[1] * 2); break;
            case 6: geo = new THREE.BoxGeometry(size[0] * 2, size[1] * 2, size[2] * 2); break;
            default: geo = new THREE.BoxGeometry(0.1, 0.1, 0.1);
        }

        const mat = new THREE.MeshStandardMaterial({
            color: 0x00ffff,
            metalness: 0.8,
            roughness: 0.2,
            emissive: 0x00ffff,
            emissiveIntensity: 0.2
        });
        const mesh = new THREE.Mesh(geo, mat);
        mesh.userData.id = i;
        mesh.castShadow = true;
        robotMeshes.add(mesh);
    }
}

async function initSession() {
    const queries = [
        `CREATE TABLE IF NOT EXISTS robot_sessions (
            session_id TEXT PRIMARY KEY,
            machine_name TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );`,
        `CREATE TABLE IF NOT EXISTS robot_frames (
            id SERIAL PRIMARY KEY,
            session_id TEXT REFERENCES robot_sessions(session_id),
            frame_index INTEGER,
            timestamp_ns BIGINT,
            joint_pos TEXT,
            joint_vel TEXT,
            torques TEXT,
            ee_pos TEXT,
            contact_force FLOAT,
            input_vec TEXT,
            grip BOOLEAN
        );`,
        `INSERT INTO robot_sessions (session_id, machine_name) VALUES ('${SESSION_ID}', 'WasmClient') ON CONFLICT DO NOTHING;`
    ];

    try {
        for (const query of queries) {
            await sqlClient.query(query, [], {});
        }
        dbReady = true;
        document.getElementById('status-dot')?.classList.add('online');
    } catch (e: any) {
        // Fallback catch
        document.getElementById('status-dot')?.classList.add('error');
    }
}

function onWindowResize() {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
}

function loop() {
    if (!isInitialized) return;

    // Physics
    try {
        // Map keys to directional forces
        const forceMag = 1500.0;
        let fx = 0;
        let fy = 0;
        if (keys['ArrowUp'] || keys['KeyW']) fy = forceMag;
        if (keys['ArrowDown'] || keys['KeyS']) fy = -forceMag;
        if (keys['ArrowRight'] || keys['KeyD']) fx = forceMag;
        if (keys['ArrowLeft'] || keys['KeyA']) fx = -forceMag;

        // Run multiple physics steps per visual frame for real-time simulation
        // Assuming visual frame is ~16ms (60fps), and physics step is 2ms (0.002s)
        const stepsPerFrame = 8;

        for (let i = 0; i < stepsPerFrame; i++) {
            // Apply generalized force to the freejoint
            // qfrc_applied is zeroed out at the end of every mj_step, so we must set it every substep
            if (state && state.qfrc_applied) {
                state.qfrc_applied[0] = fx;
                state.qfrc_applied[1] = fy;
            }

            mujoco.mj_step(model, state);
        }
    } catch (e) {
        console.error("CRITICAL PHYSICS ERROR - STOPPING SIM:", e);
        isInitialized = false; // Stop the loop
        setStatus("Simulation Crashed: Memory Limit Exceeded");
        return;
    }

    // Sync Visuals
    try {
        if (!isInitialized) return; // Exit if physics crashed above
        robotMeshes.children.forEach((mesh: any) => {
            const id = mesh.userData.id;
            const pos = state.geom_xpos.subarray(id * 3, id * 3 + 3);
            const mat = state.geom_xmat.subarray(id * 9, id * 9 + 9);

            mesh.position.set(pos[0], pos[1], pos[2]);

            const m4 = new THREE.Matrix4();
            m4.set(
                mat[0], mat[1], mat[2], 0,
                mat[3], mat[4], mat[5], 0,
                mat[6], mat[7], mat[8], 0,
                0, 0, 0, 1
            );
            mesh.quaternion.setFromRotationMatrix(m4);
        });
    } catch (e) {
        console.warn("Visual sync failed", e);
    }

    // Telemetry Buffer (Record every frame at 60Hz)
    if (isInitialized && dbReady) {
        try {
            const jpData = state.qpos;
            const jvData = state.qvel;

            if (!jpData || jpData.length < 7) throw new Error("Invalid QPos");
            if (!jvData || jvData.length < 6) throw new Error("Invalid QVel");

            const jp = Array.from(jpData.subarray(0, 7)).map((v: any) => v.toFixed(4)).join(',');
            const jv = Array.from(jvData.subarray(0, Math.min(7, jvData.length))).map((v: any) => v.toFixed(4)).join(',');

            // Reconstruct applied torque from current keys since mj_step zeroes out qfrc_applied internally
            const forceMag = 1500.0;
            let fx = 0, fy = 0;
            if (keys['ArrowUp'] || keys['KeyW']) fy = forceMag;
            if (keys['ArrowDown'] || keys['KeyS']) fy = -forceMag;
            if (keys['ArrowRight'] || keys['KeyD']) fx = forceMag;
            if (keys['ArrowLeft'] || keys['KeyA']) fx = -forceMag;
            const jt = `${fx.toFixed(4)},${fy.toFixed(4)},0.0000,0.0000,0.0000,0.0000,0.0000`;

            const padArrayStr = (str: string, targetLen: number) => {
                const arr = str.split(',');
                while (arr.length < targetLen) arr.push("0.0000");
                return arr.join(',');
            };
            const paddedJv = padArrayStr(jv, 7);
            const ee = Array.from(state.xpos.subarray((model.nbody - 1) * 3, (model.nbody - 1) * 3 + 3)).map((v: any) => v.toFixed(4)).join(',');

            const inputX = (keys['ArrowRight'] || keys['KeyD']) ? 1 : ((keys['ArrowLeft'] || keys['KeyA']) ? -1 : 0);
            const inputY = (keys['ArrowUp'] || keys['KeyW']) ? 1 : ((keys['ArrowDown'] || keys['KeyS']) ? -1 : 0);
            const inputVec = `${inputX},${inputY},0`;

            // Push to local memory buffer
            frameBuffer.push(`('${SESSION_ID}', ${frameIdx}, ${Date.now() * 1000000}, '{${jp}}', '{${paddedJv}}', '{${jt}}', '{${ee}}', 0, '{${inputVec}}', false)`);

            // Flush buffer to Database every 60 frames (~1 second)
            if (frameBuffer.length >= 60) {
                const values = frameBuffer.splice(0, frameBuffer.length).join(', ');
                const sql = `INSERT INTO robot_frames (session_id, frame_index, timestamp_ns, joint_pos, joint_vel, torques, ee_pos, contact_force, input_vec, grip) VALUES ${values};`;
                sqlClient.query(sql, [], {}).then(() => {
                    framesSent += 60;
                    const el = document.getElementById('db-rows');
                    if (el) el.innerText = `${framesSent} rows recorded`;
                }).catch((e: any) => {
                    console.error("Telemetry Error:", e.message);
                });
            }
        } catch (e) {
            console.warn("Logging failed", e);
        }
    }

    // HUD Update
    const timeVal = state ? state.time : 0;
    const timeEl = document.getElementById('time-val'); if (timeEl) timeEl.innerText = timeVal.toFixed(2) + 's';
    const frameEl = document.getElementById('frame-val'); if (frameEl) frameEl.innerText = frameIdx.toString();

    // 15 Second Session Limit
    if (timeVal >= 15.0) {
        setStatus("Session ended (15s limit reached). Press Reset to Restart.");
        isInitialized = false;
        renderer.render(scene, camera); // Final frame render
        return; // Break the animation loop
    }

    controls.update();
    renderer.render(scene, camera);
    frameIdx++;
    requestAnimationFrame(loop);
}

// @ts-ignore
window.resetSim = () => {
    if (model && state) {
        mujoco.mj_resetData(model, state);
    }

    // Clear status
    setStatus("");
    const el = document.getElementById('status-dot');
    if (el) {
        el.classList.remove('error');
        el.classList.add('online');
    }

    if (!isInitialized) {
        frameBuffer.length = 0; // Clear old buffer records
        isInitialized = true;
        // Restart the animation loop
        requestAnimationFrame(loop);
    }
};

init();
