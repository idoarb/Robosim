
#pragma once
#include <cstdint>
#include <cstdbool>

// ============================================================
// "The Truth" – exact physics state of the robot every frame
// ============================================================
#pragma pack(push, 1)
struct RobotState {
    uint64_t timestamp;            // nanoseconds since simulation start
    float    joint_positions[7];   // 7-DOF arm (radians)
    float    joint_velocities[7];  // rad/s
    float    applied_torques[7];   // N·m
    float    end_effector_pos[3];  // X,Y,Z world-space metres
    float    contact_force;        // Pressure sensor N
};

// ============================================================
// The "Action" taken by the player (Human Lead)
// ============================================================
struct PlayerAction {
    float input_vector[3];  // Movement command (normalised -1..1)
    bool  grip_status;      // false=open, true=closed
};
#pragma pack(pop)
