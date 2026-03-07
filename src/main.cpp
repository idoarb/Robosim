#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>

// Platform compatibility for Raylib vs Windows.h
#include "platform_compat.hpp"

// MuJoCo
#include <mujoco/mujoco.h>

// Custom headers
#include "robot_structs.hpp"
#include "zjson_core.hpp"
#include "pg_logger.hpp"

// rlights (Raylib helper for 4-light Phong system)
#define RLIGHTS_IMPLEMENTATION
#include "rlights.h"

// Raylib colors (if missing)
#ifndef CYAN
#define CYAN  Color{ 0, 255, 255, 255 }
#endif
#ifndef YELLOW
#define YELLOW Color{ 253, 249, 0, 255 }
#endif
#ifndef WHITE
#define WHITE  Color{ 255, 255, 255, 255 }
#endif
#ifndef RED
#define RED    Color{ 230, 41, 55, 255 }
#endif
#ifndef GREEN
#define GREEN  Color{ 0, 228, 48, 255 }
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Globals & Constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int   SCREEN_W   = 1600;
static constexpr int   SCREEN_H   = 900;
static constexpr int   SIM_DOF    = 7;
static constexpr float KP         = 85.0f;
static constexpr float KD         = 6.0f;
static constexpr float WH_W       = 44.0f;
static constexpr float WH_D       = 32.0f;
static constexpr float WH_H       = 9.0f;

// NeonDB connection string
static const char* NEON_CONN =
    "postgresql://neondb_owner:npg_o4Qqw5SsyHFM"
    "@ep-muddy-fire-ads9qx8f-pooler.c-2.us-east-1.aws.neon.tech"
    "/neondb?sslmode=require&channel_binding=require";

static mjModel* g_mj = nullptr;
static mjData*  g_md = nullptr;
static uint64_t g_t0 = 0;
static float    g_contact_smooth = 0.f;

// ─────────────────────────────────────────────────────────────────────────────
//  Time Helper
// ─────────────────────────────────────────────────────────────────────────────
static uint64_t now_ns() {
    auto n = std::chrono::high_resolution_clock::now().time_since_epoch();
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(n).count();
}

// ─────────────────────────────────────────────────────────────────────────────
//  MuJoCo Matrix Conversion
// ─────────────────────────────────────────────────────────────────────────────
static Matrix mj_mat_to_rl(const mjtNum* xmat, const mjtNum* xpos) {
    // MuJoCo: Z-up, Y-forward. Raylib: Y-up, Z-forward.
    // Row-major 3x3 -> Column-major 4x4
    Matrix m = {0};
    m.m0 = (float)xmat[0]; m.m4 = (float)xmat[1]; m.m8  = (float)xmat[2];  m.m12 = (float)xpos[0];
    m.m1 = (float)xmat[3]; m.m5 = (float)xmat[4]; m.m9  = (float)xmat[5];  m.m13 = (float)xpos[1];
    m.m2 = (float)xmat[6]; m.m6 = (float)xmat[7]; m.m10 = (float)xmat[8];  m.m14 = (float)xpos[2];
    m.m15 = 1.0f;
    
    // Swap Y and Z for Raylib
    Matrix swap = MatrixRotateX(-PI/2.0f);
    return MatrixMultiply(m, swap);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing Functions
// ─────────────────────────────────────────────────────────────────────────────
static void draw_geom_lit(int i, Shader s) {
    const mjtNum* pos = &g_md->geom_xpos[3*i];
    const mjtNum* mat = &g_md->geom_xmat[9*i];
    const mjtNum* gsz = &g_mj->geom_size[3*i];
    
    rlPushMatrix();
    Matrix m = mj_mat_to_rl(mat, pos);
    rlMultMatrixf(MatrixToFloat(m));
    
    Color c = { (unsigned char)(g_mj->geom_rgba[4*i]*255),
                (unsigned char)(g_mj->geom_rgba[4*i+1]*255),
                (unsigned char)(g_mj->geom_rgba[4*i+2]*255), 255 };
    
    BeginShaderMode(s);
    int type = g_mj->geom_type[i];
    if (type == mjGEOM_SPHERE)   DrawSphere({0,0,0}, (float)gsz[0], c);
    else if (type == mjGEOM_CAPSULE) DrawCylinder({0,0,0}, (float)gsz[0], (float)gsz[0], (float)gsz[1]*2, 16, c);
    else if (type == mjGEOM_BOX)     DrawCube({0,0,0}, (float)gsz[0]*2, (float)gsz[1]*2, (float)gsz[2]*2, c);
    else if (type == mjGEOM_CYLINDER) DrawCylinder({0,0,0}, (float)gsz[0], (float)gsz[0], (float)gsz[1]*2, 16, c);
    EndShaderMode();
    
    rlPopMatrix();
}

static void draw_robot(Shader s) {
    for (int i=0; i<g_mj->ngeom; ++i) {
        int bid = g_mj->geom_bodyid[i];
        std::string bname = mj_id2name(g_mj, mjOBJ_BODY, bid) ? mj_id2name(g_mj, mjOBJ_BODY, bid) : "";
        
        // Custom colors for robot
        if (bname.find("robot") != std::string::npos || bname.find("link") != std::string::npos) {
            // Metallic dark gunmetal
            g_mj->geom_rgba[4*i] = 0.15f;
            g_mj->geom_rgba[4*i+1] = 0.18f;
            g_mj->geom_rgba[4*i+2] = 0.22f;
        }
        draw_geom_lit(i, s);
        
        // Glowing joint origins
        if (bname.find("link") != std::string::npos) {
            Vector3 jp = {(float)g_md->xpos[3*bid], (float)g_md->xpos[3*bid+1], (float)g_md->xpos[3*bid+2]};
            // Transform to Raylib
            float ry = jp.z, rz = -jp.y;
            DrawSphereEx({jp.x, ry, rz}, 0.04f, 8, 8, {0, 255, 200, 180});
        }
    }
}

static void draw_warehouse_lit(Shader s) {
    BeginShaderMode(s);
    // Concrete floor
    DrawPlane({0,0,0}, {WH_W, WH_D}, {40,45,55,255});
    // Grid lines
    for(float i=-WH_W/2; i<=WH_W/2; i+=2.f) DrawLine3D({i,0,-WH_D/2}, {i,0,WH_D/2}, {60,65,75,100});
    for(float i=-WH_D/2; i<=WH_D/2; i+=2.f) DrawLine3D({-WH_W/2,0,i}, {WH_W/2,0,i}, {60,65,75,100});
    
    // Industrial Walls (Dark Steel)
    Color wc = {30,32,38,255};
    DrawCube({0, WH_H/2, -WH_D/2}, WH_W, WH_H, 0.2f, wc); // Back
    DrawCube({-WH_W/2, WH_H/2, 0}, 0.2f, WH_H, WH_D, wc); // Left
    DrawCube({ WH_W/2, WH_H/2, 0}, 0.2f, WH_H, WH_D, wc); // Right
    
    // Support columns (I-beams)
    for(float x=-WH_W/2+4; x<WH_W/2; x+=12.f) {
        DrawCube({x, WH_H/2, -WH_D/2+0.5f}, 0.4f, WH_H, 0.4f, {45,48,55,255});
    }
    EndShaderMode();
    
    // Hazard stripes
    for(float x=-WH_W/2+2; x<WH_W/2; x+=1.5f) {
        DrawLine3D({x, 0.01f, -WH_D/2+1}, {x+0.5f, 0.01f, -WH_D/2+2}, YELLOW);
    }
}

static void draw_hud(const RobotState& rs, const PlayerAction& pa, float sim_t, uint64_t fidx, float cratio) {
    DrawRectangle(10, 10, 300, 200, {0, 0, 0, 150});
    DrawText("ROBOT WAREHOUSE SIM", 20, 20, 20, CYAN);
    DrawText(TextFormat("Time: %.2fs", sim_t), 20, 50, 15, WHITE);
    DrawText(TextFormat("Frames: %llu", fidx), 20, 70, 15, WHITE);
    DrawText(TextFormat("Compression: %.2f:1", cratio), 20, 90, 15, GREEN);
    
    DrawText("Joints (Deg):", 20, 120, 15, YELLOW);
    for(int i=0; i<7; ++i) {
        DrawText(TextFormat("J%d: %.1f", i, rs.joint_positions[i]*57.3f), 20 + (i%4)*70, 140 + (i/4)*20, 12, WHITE);
    }
    
    DrawText(TextFormat("Grip: %s", pa.grip_status ? "CLOSED" : "OPEN"), 20, 180, 15, pa.grip_status ? RED : GREEN);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MuJoCo Initialization
// ─────────────────────────────────────────────────────────────────────────────
static const char* FALLBACK_XML = "<mujoco><worldbody><light pos='0 0 3'/><geom type='plane' size='10 10 0.1' rgba='.2 .2 .2 1'/><body name='robot' pos='0 0 1'><joint type='free'/><geom type='sphere' size='0.2' rgba='0 0.5 1 1'/></body></worldbody></mujoco>";

static bool init_mujoco(const char* path) {
    char err[1024]={};
    g_mj = mj_loadXML(path, nullptr, err, sizeof(err));
    if (!g_mj) {
        printf("[WARN] Model load failed: %s – using fallback\n", err);
        mjVFS vfs; mj_defaultVFS(&vfs);
        mj_addBufferVFS(&vfs, "f.xml", FALLBACK_XML, (int)strlen(FALLBACK_XML));
        g_mj = mj_loadXML("f.xml", &vfs, err, sizeof(err));
        mj_deleteVFS(&vfs);
    }
    if (!g_mj) return false;
    g_md = mj_makeData(g_mj);
    mj_forward(g_mj, g_md);
    return true;
}

static RobotState capture_state(uint64_t ts) {
    RobotState rs{};
    rs.timestamp = ts;
    if (!g_mj || !g_md) return rs;
    for (int i=0; i<7 && i<g_mj->nq; ++i) rs.joint_positions[i] = (float)g_md->qpos[i];
    for (int i=0; i<7 && i<g_mj->nv; ++i) rs.joint_velocities[i] = (float)g_md->qvel[i];
    for (int i=0; i<7 && i<g_mj->nu; ++i) rs.applied_torques[i] = (float)g_md->ctrl[i];
    rs.end_effector_pos[0] = (float)g_md->xpos[3*(g_mj->nbody-1)];
    rs.end_effector_pos[1] = (float)g_md->xpos[3*(g_mj->nbody-1)+1];
    rs.end_effector_pos[2] = (float)g_md->xpos[3*(g_mj->nbody-1)+2];
    rs.contact_force = 0;
    for (int i=0; i<g_md->ncon; ++i) {
        mjtNum force[6];
        mj_contactForce(g_mj, g_md, i, force);
        rs.contact_force += (float)force[0];
    }
    return rs;
}

static void apply_action(const PlayerAction& pa, float t) {
    if (!g_mj || !g_md) return;
    // Simple PD control or direct drive for demo
    for (int i=0; i<g_mj->nu; ++i) {
        float target = 0.5f * sinf(t + i*0.5f);
        if (i < 3) target += pa.input_vector[i] * 0.5f;
        float cur = (float)g_md->qpos[i];
        float vel = (float)g_md->qvel[i];
        g_md->ctrl[i] = KP * (target - cur) - KD * vel;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(SCREEN_W, SCREEN_H, "Warehouse Robot Simulation | MuJoCo + NeonDB");
    SetTargetFPS(60);

    if (!init_mujoco("../thirdparty/model/humanoid/humanoid.xml")) return 1;

    // Shader setup
    Shader shader = LoadShader("lighting.vs", "lighting.fs");
    bool use_shader = (shader.id > 0);
    if (use_shader) {
        shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");
        int ambLoc = GetShaderLocation(shader, "ambient");
        float ambient[4] = {0.1f, 0.1f, 0.12f, 1.0f};
        SetShaderValue(shader, ambLoc, ambient, SHADER_UNIFORM_VEC4);
    }

    // Lights
    Light lights[4];
    if (use_shader) {
        lights[0] = CreateLight(LIGHT_POINT, {-10, 8, -10}, {0,0,0}, WHITE, shader);
        lights[1] = CreateLight(LIGHT_POINT, { 10, 8, -10}, {0,0,0}, WHITE, shader);
        lights[2] = CreateLight(LIGHT_POINT, {-10, 8,  10}, {0,0,0}, WHITE, shader);
        lights[3] = CreateLight(LIGHT_POINT, { 10, 8,  10}, {0,0,0}, WHITE, shader);
    }

    // Logging
    FILE* log_fp = fopen("robot_session.dat", "wb");
    zjson::ZjsonWriter* zw = log_fp ? new zjson::ZjsonWriter(log_fp) : nullptr;
    
    char machine_buf[256] = "unknown";
    DWORD machine_sz = sizeof(machine_buf);
    GetComputerNameA(machine_buf, &machine_sz);
    
    neon::PgLogger pg_logger(NEON_CONN, machine_buf);
    bool pg_ok = pg_logger.start();

    Camera3D cam = { {10, 8, 10}, {0, 1.5f, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE };
    g_t0 = now_ns();
    uint64_t fidx = 0;
    float sim_t = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        UpdateCamera(&cam, CAMERA_ORBITAL);

        // Physics step
        apply_action({}, sim_t);
        mj_step(g_mj, g_md);
        sim_t += (float)g_mj->opt.timestep;

        // Capture & Log
        uint64_t ts = now_ns() - g_t0;
        RobotState rs = capture_state(ts);
        PlayerAction pa = { {0,0,0}, false };
        
        if (zw) {
            zjson::RawFrame rf{};
            rf.timestamp = rs.timestamp;
            memcpy(rf.joint_positions, rs.joint_positions, 7*4);
            zw->write(rf);
        }
        
        if (pg_ok && (fidx % 30 == 0)) {
            pg_logger.push(fidx, rs, pa);
        }

        fidx++;

        // Render
        BeginDrawing();
        ClearBackground({15, 18, 24, 255});
        
        BeginMode3D(cam);
        if (use_shader) {
            float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
            SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], vp, SHADER_UNIFORM_VEC3);
        }
        
        draw_warehouse_lit(shader);
        draw_robot(shader);
        EndMode3D();

        draw_hud(rs, pa, sim_t, fidx, 1.0f);
        
        if (pg_ok) {
            DrawText(TextFormat("DB: %llu rows", pg_logger.frames_sent()), SCREEN_W - 150, 20, 15, pg_logger.error_count() == 0 ? GREEN : RED);
        }

        EndDrawing();
    }

    if (pg_ok) pg_logger.stop();
    if (zw) delete zw;
    if (log_fp) fclose(log_fp);
    UnloadShader(shader);
    mj_deleteData(g_md);
    mj_deleteModel(g_mj);
    CloseWindow();

    return 0;
}
