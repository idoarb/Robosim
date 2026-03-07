/**
 * pg_logger.hpp  –  NeonDB logger via libpq (PostgreSQL C client)
 * =================================================================
 * Uses the official libpq library from PostgreSQL 18 installation.
 * SSL is handled automatically by libpq with sslmode=require.
 *
 * Architecture:
 *   - Main thread pushes FrameRecord into a queue (non-blocking)
 *   - Background thread flushes batches of BATCH_SIZE rows with a
 *     single multi-row parameterized INSERT
 *   - Schema is created automatically on first connection
 *
 * Tables created in NeonDB:
 *   robot_sessions  – one row per simulation run
 *   robot_frames    – one row per logged frame (throttled to ~2/sec)
 */
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <sstream>
#include <iomanip>
#include <windows.h>

// libpq header
#include <libpq-fe.h>

#include "robot_structs.hpp"

namespace neon {

// ─── Frame record buffered from main thread ───────────────────────────────────
struct FrameRecord {
    uint64_t   frame_index;
    RobotState rs;
    PlayerAction pa;
};

// ─── Helper: float array → PostgreSQL literal {v0,v1,...} ────────────────────
static std::string pg_float_arr(const float* v, int n) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6) << "{";
    for (int i = 0; i < n; ++i) { ss << v[i]; if (i < n-1) ss << ","; }
    ss << "}";
    return ss.str();
}

// ─── Main logger class ────────────────────────────────────────────────────────
class PgLogger {
public:
    static constexpr int BATCH_SIZE = 30;

    PgLogger(const char* conn_str, const char* machine_name)
        : conn_str_(conn_str), machine_(machine_name),
          conn_(nullptr), running_(false),
          frames_sent_(0), errors_(0), session_uuid_("") {}

    ~PgLogger() { if (conn_) PQfinish(conn_); }

    bool start() {
        // Generate a simple session ID
        LARGE_INTEGER li; QueryPerformanceCounter(&li);
        char buf[64];
        snprintf(buf, sizeof(buf), "%llx%llx",
                 (unsigned long long)li.QuadPart,
                 (unsigned long long)GetTickCount64());
        session_uuid_ = buf;

        running_ = true;
        worker_ = std::thread(&PgLogger::worker_func, this);
        printf("[PG] Logger started  session=%s\n", session_uuid_.c_str());
        return true;
    }

    void push(uint64_t frame_idx, const RobotState& rs, const PlayerAction& pa) {
        FrameRecord fr;
        fr.frame_index = frame_idx;
        fr.rs = rs;
        fr.pa = pa;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.push_back(fr);
        }
        cv_.notify_one();
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        // Final session update
        if (conn_ && PQstatus(conn_) == CONNECTION_OK) {
            char sql[256];
            snprintf(sql, sizeof(sql),
                "UPDATE robot_sessions SET ended_at=NOW(), total_frames=%llu"
                " WHERE session_id='%s'",
                (unsigned long long)frames_sent_, session_uuid_.c_str());
            PGresult* r = PQexec(conn_, sql);
            if (r) PQclear(r);
        }
        printf("[PG] Logger stopped  rows_sent=%llu  errors=%llu\n",
               (unsigned long long)frames_sent_, (unsigned long long)errors_);
    }

    uint64_t frames_sent() const { return frames_sent_; }
    uint64_t error_count() const { return errors_; }

private:
    std::string      conn_str_, machine_, session_uuid_;
    PGconn*          conn_;
    std::vector<FrameRecord> queue_;
    std::mutex       mtx_;
    std::condition_variable cv_;
    std::thread      worker_;
    std::atomic<bool>     running_;
    std::atomic<uint64_t> frames_sent_, errors_;

    bool connect() {
        if (conn_) PQfinish(conn_);
        conn_ = PQconnectdb(conn_str_.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            // Write to both stdout and a debug file
            const char* err = PQerrorMessage(conn_);
            printf("[PG] Connection FAILED: %s\n", err);
            FILE* dbg = fopen("pg_debug.log","w");
            if (dbg) { fprintf(dbg,"ConnStr: %s\nError: %s\n",conn_str_.c_str(),err); fclose(dbg); }
            PQfinish(conn_);
            conn_ = nullptr;
            ++errors_;
            return false;
        }
        printf("[PG] Connected  server=%s\n",
               PQparameterStatus(conn_, "server_version"));
        FILE* dbg = fopen("pg_debug.log","w");
        if (dbg) { fprintf(dbg,"OK connected server=%s\n",
                           PQparameterStatus(conn_,"server_version")); fclose(dbg); }
        return true;
    }

    bool exec(const char* sql, const char** vals, int n) {
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            if (!connect()) return false;
        }
        PGresult* r = PQexecParams(conn_, sql, n,
                                   nullptr, vals, nullptr, nullptr, 0);
        bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK ||
                   PQresultStatus(r) == PGRES_TUPLES_OK);
        if (!ok) {
            printf("[PG] Query error: %s\n", PQerrorMessage(conn_));
            ++errors_;
        }
        PQclear(r);
        return ok;
    }

    bool exec_simple(const char* sql) {
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            if (!connect()) return false;
        }
        PGresult* r = PQexec(conn_, sql);
        bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK ||
                   PQresultStatus(r) == PGRES_TUPLES_OK);
        if (!ok) {
            printf("[PG] SQL error: %s\n", PQerrorMessage(conn_));
            ++errors_;
        }
        PQclear(r);
        return ok;
    }

    void ensure_schema() {
        exec_simple(
            "CREATE TABLE IF NOT EXISTS robot_sessions ("
            "  session_id    TEXT PRIMARY KEY,"
            "  machine_name  TEXT,"
            "  started_at    TIMESTAMPTZ DEFAULT NOW(),"
            "  ended_at      TIMESTAMPTZ,"
            "  total_frames  BIGINT DEFAULT 0"
            ")");

        exec_simple(
            "CREATE TABLE IF NOT EXISTS robot_frames ("
            "  id             BIGSERIAL PRIMARY KEY,"
            "  session_id     TEXT REFERENCES robot_sessions(session_id),"
            "  frame_index    BIGINT NOT NULL,"
            "  timestamp_ns   BIGINT NOT NULL,"
            "  joint_pos      FLOAT4[] NOT NULL,"
            "  joint_vel      FLOAT4[] NOT NULL,"
            "  torques        FLOAT4[] NOT NULL,"
            "  ee_pos         FLOAT4[] NOT NULL,"
            "  contact_force  FLOAT4 NOT NULL,"
            "  input_vec      FLOAT4[] NOT NULL,"
            "  grip           BOOLEAN NOT NULL,"
            "  created_at     TIMESTAMPTZ DEFAULT NOW()"
            ")");

        exec_simple(
            "CREATE INDEX IF NOT EXISTS idx_rf_session "
            "ON robot_frames(session_id)");
    }

    void insert_session() {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "INSERT INTO robot_sessions(session_id,machine_name)"
            " VALUES('%s','%s') ON CONFLICT(session_id) DO NOTHING",
            session_uuid_.c_str(), machine_.c_str());
        exec_simple(sql);
    }

    void flush_batch(const std::vector<FrameRecord>& batch) {
        if (batch.empty() || !conn_) return;

        // Build: BEGIN; INSERT ...row1...; INSERT ...row2...; COMMIT;
        // Using individual parameterized inserts inside a transaction
        // (simpler than building a giant multi-row query with many $N params)
        if (!exec_simple("BEGIN")) return;

        bool ok = true;
        for (const auto& fr : batch) {
            // Prepare string values for all array columns
            std::string jp  = pg_float_arr(fr.rs.joint_positions,  7);
            std::string jv  = pg_float_arr(fr.rs.joint_velocities, 7);
            std::string tq  = pg_float_arr(fr.rs.applied_torques,  7);
            std::string ee  = pg_float_arr(fr.rs.end_effector_pos, 3);
            std::string iv  = pg_float_arr(fr.pa.input_vector,     3);

            char fi[32], ts[32], cf[32];
            snprintf(fi, sizeof(fi), "%llu", (unsigned long long)fr.frame_index);
            snprintf(ts, sizeof(ts), "%llu", (unsigned long long)fr.rs.timestamp);
            snprintf(cf, sizeof(cf), "%f",   fr.rs.contact_force);

            const char* grip = fr.pa.grip_status ? "true" : "false";

            const char* vals[10] = {
                session_uuid_.c_str(),
                fi, ts,
                jp.c_str(), jv.c_str(), tq.c_str(), ee.c_str(),
                cf,
                iv.c_str(),
                grip
            };

            ok = exec(
                "INSERT INTO robot_frames"
                "(session_id,frame_index,timestamp_ns,"
                " joint_pos,joint_vel,torques,ee_pos,"
                " contact_force,input_vec,grip)"
                " VALUES($1,$2::BIGINT,$3::BIGINT,"
                " $4::FLOAT4[],$5::FLOAT4[],$6::FLOAT4[],$7::FLOAT4[],"
                " $8::FLOAT4,$9::FLOAT4[],$10::BOOLEAN)",
                vals, 10);

            if (!ok) break;
        }

        exec_simple(ok ? "COMMIT" : "ROLLBACK");
        if (ok) frames_sent_ += (uint64_t)batch.size();
    }

    void worker_func() {
        if (!connect()) {
            printf("[PG] Initial connection failed – logger disabled\n");
            return;
        }
        ensure_schema();
        insert_session();

        std::vector<FrameRecord> local;
        while (running_ || !queue_.empty()) {
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(500),
                             [&]{ return !queue_.empty() || !running_; });
                if (queue_.size() >= (size_t)BATCH_SIZE ||
                    (!running_ && !queue_.empty())) {
                    local.swap(queue_);
                }
            }
            if (!local.empty()) {
                flush_batch(local);
                local.clear();
            }
        }
    }
};

} // namespace neon
