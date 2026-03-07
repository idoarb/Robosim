/*
 * zjson_decode.cpp  –  offline decoder / analyser for robot_session.dat
 *
 * Usage:  decode.exe robot_session.dat
 *
 * Prints each frame in human-readable CSV and also validates CRC32.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "../src/zjson_core.hpp"

static void print_frame(uint64_t idx, const zjson::RawFrame& f, bool is_kf, bool crc_ok) {
    printf("F%06llu %s CRC=%s  t=%llu\n",
           (unsigned long long)idx,
           is_kf ? "KEY" : "dlt",
           crc_ok ? "OK" : "FAIL",
           (unsigned long long)f.timestamp);
    printf("  joints  pos: %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f\n",
           f.joint_positions[0], f.joint_positions[1], f.joint_positions[2],
           f.joint_positions[3], f.joint_positions[4], f.joint_positions[5],
           f.joint_positions[6]);
    printf("  joints  vel: %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f\n",
           f.joint_velocities[0], f.joint_velocities[1], f.joint_velocities[2],
           f.joint_velocities[3], f.joint_velocities[4], f.joint_velocities[5],
           f.joint_velocities[6]);
    printf("  torques    : %+.1f %+.1f %+.1f %+.1f %+.1f %+.1f %+.1f\n",
           f.applied_torques[0], f.applied_torques[1], f.applied_torques[2],
           f.applied_torques[3], f.applied_torques[4], f.applied_torques[5],
           f.applied_torques[6]);
    printf("  ee_pos     : %+.4f %+.4f %+.4f  contact=%.1fN\n",
           f.end_effector_pos[0], f.end_effector_pos[1], f.end_effector_pos[2],
           f.contact_force);
    printf("  input      : %+.3f %+.3f %+.3f  grip=%s\n",
           f.input_vector[0], f.input_vector[1], f.input_vector[2],
           f.grip_status ? "CLOSED" : "OPEN");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: decode <robot_session.dat>\n");
        return 1;
    }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror("open"); return 1; }

    // Check magic
    char magic[8];
    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, "ZJRCv1.0", 8) != 0) {
        fprintf(stderr, "[ERROR] Not a valid zjson-core file\n");
        fclose(fp); return 1;
    }
    printf("[zjson-core decoder]  File: %s\n\n", argv[1]);

    zjson::RawFrame cur{}, prev{};
    uint64_t fidx         = 0;
    uint64_t crc_errors   = 0;
    uint64_t keyframes    = 0;
    uint64_t delta_frames = 0;

    while (true) {
        // Read flags byte
        uint8_t flags;
        if (fread(&flags, 1, 1, fp) != 1) break;
        bool is_kf     = (flags & 0x01) != 0;
        bool grip      = (flags & 0x02) != 0;

        // Record start for CRC (re-read approach: store raw bytes)
        std::vector<uint8_t> frame_raw;
        frame_raw.push_back(flags);

        auto read8 = [&]() -> bool {
            uint8_t b; if (fread(&b,1,1,fp)!=1) return false;
            frame_raw.push_back(b); return true;
        };

        // timestamp (8 bytes)
        uint64_t ts = 0;
        for (int i = 0; i < 8; ++i) { if (!read8()) goto done; }
        {
            size_t sz = frame_raw.size();
            memcpy(&ts, frame_raw.data() + sz - 8, 8);
        }
        cur.timestamp = ts;
        cur.grip_status = grip;

        if (is_kf) {
            // Full floats
            auto readf = [&](float& v) -> bool {
                for (int i = 0; i < 4; ++i) if (!read8()) return false;
                size_t n = frame_raw.size();
                memcpy(&v, frame_raw.data()+n-4, 4);
                return true;
            };
            for (int i = 0; i < 7; ++i) if (!readf(cur.joint_positions[i]))  goto done;
            for (int i = 0; i < 7; ++i) if (!readf(cur.joint_velocities[i])) goto done;
            for (int i = 0; i < 7; ++i) if (!readf(cur.applied_torques[i]))  goto done;
            for (int i = 0; i < 3; ++i) if (!readf(cur.end_effector_pos[i])) goto done;
            if (!readf(cur.contact_force)) goto done;
            for (int i = 0; i < 3; ++i) if (!readf(cur.input_vector[i]))     goto done;
            keyframes++;
        } else {
            // Delta int16
            auto read16 = [&](int16_t& v) -> bool {
                for (int i = 0; i < 2; ++i) if (!read8()) return false;
                size_t n = frame_raw.size();
                memcpy(&v, frame_raw.data()+n-2, 2);
                return true;
            };
            int16_t d;
            for (int i=0;i<7;++i){if(!read16(d))goto done; cur.joint_positions[i]  = prev.joint_positions[i]  + zjson::dequantise(d,zjson::QUANT_SCALE_POS);}
            for (int i=0;i<7;++i){if(!read16(d))goto done; cur.joint_velocities[i] = prev.joint_velocities[i] + zjson::dequantise(d,zjson::QUANT_SCALE_VEL);}
            for (int i=0;i<7;++i){if(!read16(d))goto done; cur.applied_torques[i]  = prev.applied_torques[i]  + zjson::dequantise(d,zjson::QUANT_SCALE_TRQ);}
            for (int i=0;i<3;++i){if(!read16(d))goto done; cur.end_effector_pos[i] = prev.end_effector_pos[i] + zjson::dequantise(d,zjson::QUANT_SCALE_POS);}
            if(!read16(d))goto done; cur.contact_force=prev.contact_force+zjson::dequantise(d,zjson::QUANT_SCALE_TRQ);
            for (int i=0;i<3;++i){if(!read16(d))goto done; cur.input_vector[i]=prev.input_vector[i]+zjson::dequantise(d,zjson::QUANT_SCALE_VEL);}
            delta_frames++;
        }

        // CRC check (last 4 bytes of frame_raw before reading the stored CRC)
        uint32_t stored_crc = 0;
        {
            uint8_t cb[4];
            if (fread(cb,1,4,fp)!=4) goto done;
            memcpy(&stored_crc, cb, 4);
        }
        uint32_t calc_crc = zjson::crc32(frame_raw.data(), frame_raw.size());
        bool crc_ok = (calc_crc == stored_crc);
        if (!crc_ok) crc_errors++;

        // Print first 10 frames and every 100th
        if (fidx < 10 || fidx % 100 == 0)
            print_frame(fidx, cur, is_kf, crc_ok);

        prev = cur;
        ++fidx;
    }

done:
    fclose(fp);
    printf("\n─────────────────────────────────────\n");
    printf("Total frames  : %llu\n", (unsigned long long)fidx);
    printf("Keyframes     : %llu\n", (unsigned long long)keyframes);
    printf("Delta frames  : %llu\n", (unsigned long long)delta_frames);
    printf("CRC errors    : %llu\n", (unsigned long long)crc_errors);
    return 0;
}
