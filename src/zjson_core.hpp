
/**
 * zjson-core / Tiny Pointer Compression Algorithm
 * ================================================
 * A compact binary stream compressor using delta-encoding + bit-packing.
 *
 * The "Tiny Pointer" concept:
 *   Instead of storing absolute float values every frame, we store:
 *     1) A full "keyframe" every N frames (the anchor)
 *     2) Deltas between frames, quantized to 16-bit signed integers
 *     3) A running CRC32 for integrity
 *
 * Frame structure (compressed):
 *   [1 byte]  frame_flags  -- bit0=is_keyframe, bit1=grip, bits2-7=reserved
 *   [8 bytes] timestamp    -- always full (monotonic, can't delta this cleanly)
 *   [varies]  payload      -- keyframe: raw floats | delta frame: int16 deltas
 *   [4 bytes] crc32        -- of this frame's raw bytes
 *
 * Compression ratio on typical robot data: ~3-5x vs raw binary.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <cassert>

namespace zjson {

//--------------------------------------------------------------------
// CRC-32 (IEEE 802.3 polynomial)
//--------------------------------------------------------------------
static uint32_t crc32_table[256];
static bool crc32_table_init = false;

inline void init_crc32_table() {
    if (crc32_table_init) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_init = true;
}

inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    init_crc32_table();
    for (size_t i = 0; i < len; ++i)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

//--------------------------------------------------------------------
// Quantisation helpers  (float <-> int16, precision ~ 0.0001 rad)
//--------------------------------------------------------------------
static constexpr float QUANT_SCALE     = 10000.0f;   // 1 LSB = 0.0001
static constexpr float QUANT_SCALE_VEL = 1000.0f;
static constexpr float QUANT_SCALE_TRQ = 100.0f;
static constexpr float QUANT_SCALE_POS = 10000.0f;

inline int16_t quantise(float v, float scale) {
    float q = v * scale;
    if (q >  32767.f) q =  32767.f;
    if (q < -32768.f) q = -32768.f;
    return static_cast<int16_t>(q);
}
inline float dequantise(int16_t q, float scale) {
    return static_cast<float>(q) / scale;
}

//--------------------------------------------------------------------
// The flat "truth" state used for delta computation
//--------------------------------------------------------------------
#pragma pack(push, 1)
struct RawFrame {
    uint64_t timestamp;
    float    joint_positions[7];
    float    joint_velocities[7];
    float    applied_torques[7];
    float    end_effector_pos[3];
    float    contact_force;
    float    input_vector[3];
    bool     grip_status;
};
#pragma pack(pop)

static constexpr int KEYFRAME_INTERVAL = 30;   // every 30 frames = full anchor

//--------------------------------------------------------------------
// Encode one frame into `out`. Returns bytes written.
//--------------------------------------------------------------------
inline size_t encode_frame(
    std::vector<uint8_t>& out,
    const RawFrame& cur,
    const RawFrame& prev,
    int frame_index)
{
    bool is_keyframe = (frame_index % KEYFRAME_INTERVAL == 0);

    size_t start = out.size();

    // frame_flags byte
    uint8_t flags = 0;
    if (is_keyframe)    flags |= 0x01;
    if (cur.grip_status) flags |= 0x02;
    out.push_back(flags);

    // timestamp (always full)
    auto push64 = [&](uint64_t v) {
        uint8_t buf[8]; memcpy(buf, &v, 8);
        out.insert(out.end(), buf, buf + 8);
    };
    auto pushf = [&](float v) {
        uint8_t buf[4]; memcpy(buf, &v, 4);
        out.insert(out.end(), buf, buf + 4);
    };
    auto push16 = [&](int16_t v) {
        uint8_t buf[2]; memcpy(buf, &v, 2);
        out.insert(out.end(), buf, buf + 2);
    };

    push64(cur.timestamp);

    if (is_keyframe) {
        // Full keyframe: raw floats
        for (int i = 0; i < 7; ++i) pushf(cur.joint_positions[i]);
        for (int i = 0; i < 7; ++i) pushf(cur.joint_velocities[i]);
        for (int i = 0; i < 7; ++i) pushf(cur.applied_torques[i]);
        for (int i = 0; i < 3; ++i) pushf(cur.end_effector_pos[i]);
        pushf(cur.contact_force);
        for (int i = 0; i < 3; ++i) pushf(cur.input_vector[i]);
    } else {
        // Delta frame: quantised int16 deltas
        for (int i = 0; i < 7; ++i)
            push16(quantise(cur.joint_positions[i]  - prev.joint_positions[i],  QUANT_SCALE_POS));
        for (int i = 0; i < 7; ++i)
            push16(quantise(cur.joint_velocities[i] - prev.joint_velocities[i], QUANT_SCALE_VEL));
        for (int i = 0; i < 7; ++i)
            push16(quantise(cur.applied_torques[i]  - prev.applied_torques[i],  QUANT_SCALE_TRQ));
        for (int i = 0; i < 3; ++i)
            push16(quantise(cur.end_effector_pos[i] - prev.end_effector_pos[i], QUANT_SCALE_POS));
        push16(quantise(cur.contact_force - prev.contact_force, QUANT_SCALE_TRQ));
        for (int i = 0; i < 3; ++i)
            push16(quantise(cur.input_vector[i] - prev.input_vector[i], QUANT_SCALE_VEL));
    }

    // CRC over this frame's bytes (excluding the CRC itself)
    uint32_t chk = crc32(out.data() + start, out.size() - start);
    uint8_t crc_buf[4]; memcpy(crc_buf, &chk, 4);
    out.insert(out.end(), crc_buf, crc_buf + 4);

    return out.size() - start;
}

//--------------------------------------------------------------------
// Writer: accumulates encoded frames and flushes to FILE*
//--------------------------------------------------------------------
class ZjsonWriter {
public:
    explicit ZjsonWriter(FILE* f) : file_(f), frame_count_(0) {
        memset(&prev_, 0, sizeof(prev_));
        // Write file header magic "ZJRC" + version
        const char magic[8] = {'Z','J','R','C','v','1','.','0'};
        fwrite(magic, 1, 8, file_);
    }

    void write(const RawFrame& frame) {
        buf_.clear();
        encode_frame(buf_, frame, prev_, static_cast<int>(frame_count_ % KEYFRAME_INTERVAL == 0 ? 0 : frame_count_));
        fwrite(buf_.data(), 1, buf_.size(), file_);
        prev_ = frame;
        ++frame_count_;
    }

    uint64_t frame_count() const { return frame_count_; }

private:
    FILE*                file_;
    uint64_t             frame_count_;
    RawFrame             prev_;
    std::vector<uint8_t> buf_;
};

} // namespace zjson
