#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <string>
#include "pager.h"
#include "buffer_pool.h"
#include "btree.h"
#include "sql_engine.h"

using namespace std::chrono;
using tp = high_resolution_clock::time_point;
static double to_ms(tp a, tp b) { return duration<double, std::milli>(b - a).count(); }
static double to_us(tp a, tp b) { return duration<double, std::micro>(b - a).count(); }

// ── pretty printing ───────────────────────────────────────────────────────────

static void bar(double value, double max_val, int width = 28) {
    int filled = (max_val > 0) ? std::min(width, (int)(value / max_val * width)) : 0;
    std::cout << "|";
    for (int i = 0; i < filled; i++) std::cout << "#";
    for (int i = filled; i < width; i++) std::cout << " ";
    std::cout << "|";
}
static void row_out(const std::string& label, double val, double ref, const std::string& unit) {
    std::cout << "  " << std::left << std::setw(26) << label;
    bar(val, ref);
    std::cout << " " << std::fixed << std::setprecision(2) << val << " " << unit << "\n";
}
static std::string comma(int64_t v) {
    std::string s = std::to_string(v);
    int i = (int)s.size() - 3;
    while (i > 0) { s.insert(i, ","); i -= 3; }
    return s;
}
static void section(const std::string& title) {
    std::cout << "\n+- " << title << " ";
    for (int i = (int)title.size() + 3; i < 58; i++) std::cout << "-";
    std::cout << "+\n";
}

struct Result { double total_ms, ops_per_sec, ns_per_op; };

// ── raw BTree benchmarks ──────────────────────────────────────────────────────

static Result bench_seq_insert(uint32_t N) {
    std::remove("bench_raw.db");
    Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
    auto t0 = high_resolution_clock::now();
    for (uint32_t i = 1; i <= N; i++) t.insert(i, i * 2);
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}
static Result bench_rnd_insert(uint32_t N) {
    std::vector<uint32_t> keys(N); std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(42); std::shuffle(keys.begin(), keys.end(), rng);
    std::remove("bench_raw.db");
    Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
    auto t0 = high_resolution_clock::now();
    for (uint32_t k : keys) t.insert(k, k * 2);
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}
static Result bench_seq_find(uint32_t N) {
    std::remove("bench_raw.db");
    { Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
      for (uint32_t i = 1; i <= N; i++) t.insert(i, i * 2); }
    Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
    volatile int32_t sink = 0;
    auto t0 = high_resolution_clock::now();
    for (uint32_t i = 1; i <= N; i++) sink = t.find(i);
    (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}
static Result bench_rnd_find(uint32_t N) {
    std::remove("bench_raw.db");
    { Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
      for (uint32_t i = 1; i <= N; i++) t.insert(i, i * 2); }
    std::vector<uint32_t> keys(N); std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(42); std::shuffle(keys.begin(), keys.end(), rng);
    Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
    volatile int32_t sink = 0;
    auto t0 = high_resolution_clock::now();
    for (uint32_t k : keys) sink = t.find(k);
    (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}

// ── SQL layer benchmarks ──────────────────────────────────────────────────────

static void build_sql_table(SQLEngine& db, uint32_t N) {
    db.exec("CREATE TABLE bench (id INT, name TEXT(32), score INT)");
    char buf[32];
    for (uint32_t i = 1; i <= N; i++) {
        std::snprintf(buf, sizeof(buf), "user%u", i);
        db.exec("INSERT INTO bench VALUES (" + std::to_string(i) +
                ", '" + buf + "', " + std::to_string(i * 3) + ")");
    }
}

static Result bench_sql_insert(uint32_t N) {
    std::remove("bench.db");
    SQLEngine db;
    auto t0 = high_resolution_clock::now();
    build_sql_table(db, N);
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}

static Result bench_sql_scan(SQLEngine& db, uint32_t N) {
    volatile size_t sink = 0;
    auto t0 = high_resolution_clock::now();
    auto res = db.select("SELECT * FROM bench");
    sink = res.rows.size(); (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}

static Result bench_sql_point(SQLEngine& db, uint32_t N, uint32_t samples) {
    std::mt19937 rng(77);
    std::uniform_int_distribution<uint32_t> dist(1, N);
    std::vector<uint32_t> keys(samples);
    for (auto& k : keys) k = dist(rng);
    volatile size_t sink = 0;
    auto t0 = high_resolution_clock::now();
    for (uint32_t k : keys) {
        auto res = db.select("SELECT id, name FROM bench WHERE id = " + std::to_string(k));
        sink += res.rows.size();
    }
    (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, samples / (ms / 1000.0), ms * 1e6 / samples};
}

static Result bench_sql_filter(SQLEngine& db, uint32_t N) {
    volatile size_t sink = 0;
    auto t0 = high_resolution_clock::now();
    auto res = db.select("SELECT id, name FROM bench WHERE score > " +
                         std::to_string((N / 2) * 3));
    sink = res.rows.size(); (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    return {ms, N / (ms / 1000.0), ms * 1e6 / N};
}

static Result bench_sql_delete(SQLEngine& db, uint32_t N) {
    // Delete the top quarter of rows by score.
    uint32_t threshold = (N - N / 4) * 3;
    auto t0 = high_resolution_clock::now();
    db.exec("DELETE FROM bench WHERE score > " + std::to_string(threshold));
    auto t1 = high_resolution_clock::now();
    double ms = to_ms(t0, t1);
    uint32_t deleted = N / 4;
    return {ms, deleted / (ms / 1000.0), ms * 1e6 / deleted};
}

// ── JOIN benchmark ────────────────────────────────────────────────────────────

static Result bench_sql_join(uint32_t N_users, uint32_t N_orders) {
    std::remove("users.db");
    std::remove("orders.db");
    SQLEngine db;
    db.exec("CREATE TABLE users (id INT, name TEXT(32))");
    db.exec("CREATE TABLE orders (oid INT, uid INT, amount INT)");

    char buf[32];
    for (uint32_t i = 1; i <= N_users; i++) {
        std::snprintf(buf, sizeof(buf), "user%u", i);
        db.exec("INSERT INTO users VALUES (" + std::to_string(i) +
                ", '" + buf + "')");
    }
    std::mt19937 rng(99);
    std::uniform_int_distribution<uint32_t> uid_dist(1, N_users);
    for (uint32_t i = 1; i <= N_orders; i++) {
        db.exec("INSERT INTO orders VALUES (" + std::to_string(i) +
                ", " + std::to_string(uid_dist(rng)) +
                ", " + std::to_string(i * 7 % 500) + ")");
    }

    volatile size_t sink = 0;
    auto t0 = high_resolution_clock::now();
    auto res = db.select("SELECT users.name, orders.amount FROM users "
                         "JOIN orders ON users.id = orders.uid");
    sink = res.rows.size(); (void)sink;
    auto t1 = high_resolution_clock::now();
    double ms = std::max(to_ms(t0, t1), 0.001); // floor at 1us to avoid div/0
    // throughput = output rows produced per second
    uint32_t out_rows = (uint32_t)res.rows.size();
    return {ms, out_rows / (ms / 1000.0), ms * 1e6 / std::max(out_rows, 1u)};
}

// ── latency percentiles ───────────────────────────────────────────────────────

static void bench_btree_percentiles(uint32_t N, uint32_t samples) {
    std::remove("bench_raw.db");
    { Pager p("bench_raw.db"); BufferPool bp(&p, 512); BTree t(&bp);
      for (uint32_t i = 1; i <= N; i++) t.insert(i, i * 2); }
    Pager pager("bench_raw.db"); BufferPool pool(&pager, 512); BTree tree(&pool);
    std::mt19937 rng(99);
    std::uniform_int_distribution<uint32_t> dist(1, N);
    std::vector<double> lats; lats.reserve(samples);
    for (uint32_t s = 0; s < samples; s++) {
        uint32_t k = dist(rng);
        auto t0 = high_resolution_clock::now();
        volatile int32_t v = tree.find(k); (void)v;
        auto t1 = high_resolution_clock::now();
        lats.push_back(to_us(t0, t1));
    }
    std::sort(lats.begin(), lats.end());
    auto pct = [&](double p) { return lats[(size_t)(p / 100.0 * lats.size())]; };
    double mx = lats.back();
    section("BTree Raw Find Latency (" + std::to_string(samples) +
            " random, N=" + std::to_string(N) + ")");
    row_out("p50",   pct(50),   mx, "us");
    row_out("p75",   pct(75),   mx, "us");
    row_out("p90",   pct(90),   mx, "us");
    row_out("p99",   pct(99),   mx, "us");
    row_out("p99.9", pct(99.9), mx, "us");
    row_out("max",   mx,        mx, "us");
    std::cout << "+----------------------------------------------------------+\n";
}

static void bench_sql_percentiles(SQLEngine& db, uint32_t N, uint32_t samples) {
    std::mt19937 rng(99);
    std::uniform_int_distribution<uint32_t> dist(1, N);
    std::vector<double> lats; lats.reserve(samples);
    for (uint32_t s = 0; s < samples; s++) {
        uint32_t k = dist(rng);
        auto t0 = high_resolution_clock::now();
        volatile size_t v = db.select(
            "SELECT id FROM bench WHERE id = " + std::to_string(k)).rows.size();
        (void)v;
        auto t1 = high_resolution_clock::now();
        lats.push_back(to_us(t0, t1));
    }
    std::sort(lats.begin(), lats.end());
    auto pct = [&](double p) { return lats[(size_t)(p / 100.0 * lats.size())]; };
    double mx = lats.back();
    section("SQL Point-SELECT Latency (" + std::to_string(samples) +
            " random, N=" + std::to_string(N) + ")");
    row_out("p50",   pct(50),   mx, "us");
    row_out("p75",   pct(75),   mx, "us");
    row_out("p90",   pct(90),   mx, "us");
    row_out("p99",   pct(99),   mx, "us");
    row_out("p99.9", pct(99.9), mx, "us");
    row_out("max",   mx,        mx, "us");
    std::cout << "+----------------------------------------------------------+\n";
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    // N values chosen to complete in <10s on Windows (MSYS2/UCRT64).
    // On Linux these finish in ~1s; raise them freely if you want more data.
    const uint32_t N_RAW     = 50000;
    const uint32_t N_SQL     = 10000;
    const uint32_t N_USERS   = 500;
    const uint32_t N_ORDERS  = 2000;
    const uint32_t PT_SAMPLES = 500;

    std::cout << "\n+==========================================================+\n";
    std::cout <<   "|      B-TREE + SQL LAYER  PERFORMANCE BENCHMARK          |\n";
    std::cout <<   "|  PAGE_SIZE=" << std::setw(5) << PAGE_SIZE
              <<   "  NODE_MAX_CELLS=" << std::setw(3) << NODE_MAX_CELLS << "  |\n";
    std::cout <<   "+==========================================================+\n";

    // ── 1. Raw BTree ─────────────────────────────────────────────────────────
    section("Raw BTree Throughput (ops/sec)  N=" + comma(N_RAW));
    auto si = bench_seq_insert(N_RAW);
    auto ri = bench_rnd_insert(N_RAW);
    auto sf = bench_seq_find(N_RAW);
    auto rf = bench_rnd_find(N_RAW);
    double mx_raw = std::max({si.ops_per_sec, ri.ops_per_sec,
                               sf.ops_per_sec, rf.ops_per_sec});
    row_out("Seq Insert",  si.ops_per_sec, mx_raw, "ops/s (" + comma((int64_t)si.ops_per_sec) + ")");
    row_out("Rand Insert", ri.ops_per_sec, mx_raw, "ops/s (" + comma((int64_t)ri.ops_per_sec) + ")");
    row_out("Seq Find",    sf.ops_per_sec, mx_raw, "ops/s (" + comma((int64_t)sf.ops_per_sec) + ")");
    row_out("Rand Find",   rf.ops_per_sec, mx_raw, "ops/s (" + comma((int64_t)rf.ops_per_sec) + ")");
    std::cout << "+----------------------------------------------------------+\n";
    std::remove("bench_raw.db");

    // ── 2. SQL layer ─────────────────────────────────────────────────────────
    section("SQL Layer Throughput  N=" + comma(N_SQL));
    std::cout << "  [INSERT " << comma(N_SQL) << " rows...]\n";
    auto sql_ins = bench_sql_insert(N_SQL);

    // Reuse the same DB for scan / point / filter / delete / percentiles.
    SQLEngine db;
    // bench.db already populated by bench_sql_insert above.

    std::cout << "  [SELECT * full scan...]\n";
    auto sql_scan = bench_sql_scan(db, N_SQL);

    std::cout << "  [point SELECT x" << PT_SAMPLES << "...]\n";
    auto sql_pt = bench_sql_point(db, N_SQL, PT_SAMPLES);

    std::cout << "  [filter SELECT WHERE score > N/2...]\n";
    auto sql_flt = bench_sql_filter(db, N_SQL);

    std::cout << "  [DELETE top-quarter rows...]\n";
    auto sql_del = bench_sql_delete(db, N_SQL);

    double mx_sql = std::max({sql_ins.ops_per_sec, sql_scan.ops_per_sec,
                               sql_pt.ops_per_sec,  sql_flt.ops_per_sec,
                               sql_del.ops_per_sec});
    row_out("INSERT (parse+exec)",      sql_ins.ops_per_sec,  mx_sql, "rows/s (" + comma((int64_t)sql_ins.ops_per_sec)  + ")");
    row_out("SELECT * (full scan)",     sql_scan.ops_per_sec, mx_sql, "rows/s (" + comma((int64_t)sql_scan.ops_per_sec) + ")");
    row_out("SELECT WHERE id=k (pt.)",  sql_pt.ops_per_sec,   mx_sql, "ops/s  (" + comma((int64_t)sql_pt.ops_per_sec)  + ")");
    row_out("SELECT WHERE score>N",     sql_flt.ops_per_sec,  mx_sql, "rows/s (" + comma((int64_t)sql_flt.ops_per_sec) + ")");
    row_out("DELETE WHERE score>N",     sql_del.ops_per_sec,  mx_sql, "rows/s (" + comma((int64_t)sql_del.ops_per_sec) + ")");
    std::cout << "+----------------------------------------------------------+\n";

    // ── 3. JOIN ──────────────────────────────────────────────────────────────
    section("JOIN Throughput  users=" + comma(N_USERS) +
            "  orders=" + comma(N_ORDERS));
    std::cout << "  [building tables + running nested-loop join...]\n";
    auto sql_join = bench_sql_join(N_USERS, N_ORDERS);
    double total_pairs = (double)N_USERS * N_ORDERS;
    std::cout << "  Join completed in " << std::fixed << std::setprecision(2)
              << sql_join.total_ms << " ms  ("
              << comma((int64_t)sql_join.ops_per_sec) << " output rows/s)\n";
    std::cout << "  Pairs evaluated : " << comma((int64_t)total_pairs) << "\n";
    std::cout << "  Pairs/sec       : "
              << comma((int64_t)(total_pairs / (sql_join.total_ms / 1000.0))) << "\n";
    std::cout << "+----------------------------------------------------------+\n";
    std::remove("users.db");
    std::remove("orders.db");

    // ── 4. Avg latency ───────────────────────────────────────────────────────
    section("Avg Latency / op  (ns, lower = better)");
    double mx_ns = std::max({si.ns_per_op, ri.ns_per_op, sf.ns_per_op, rf.ns_per_op,
                              sql_ins.ns_per_op, sql_pt.ns_per_op, sql_del.ns_per_op});
    row_out("BTree Seq Insert",        si.ns_per_op,      mx_ns, "ns");
    row_out("BTree Rand Insert",       ri.ns_per_op,      mx_ns, "ns");
    row_out("BTree Seq Find",          sf.ns_per_op,      mx_ns, "ns");
    row_out("BTree Rand Find",         rf.ns_per_op,      mx_ns, "ns");
    row_out("SQL INSERT (full stack)", sql_ins.ns_per_op, mx_ns, "ns");
    row_out("SQL Point-SELECT",        sql_pt.ns_per_op,  mx_ns, "ns");
    row_out("SQL DELETE (per row)",    sql_del.ns_per_op, mx_ns, "ns");
    std::cout << "+----------------------------------------------------------+\n";

    // ── 5. Percentile latency ─────────────────────────────────────────────────
    bench_btree_percentiles(N_RAW, PT_SAMPLES);
    bench_sql_percentiles(db, N_SQL - N_SQL / 4, PT_SAMPLES); // account for deleted rows

    std::cout << "\n[OK]  Benchmark complete.\n\n";

    std::remove("bench.db");
    std::remove("bench_raw.db");
    return 0;
}