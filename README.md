# SQL-Mini

A minimal relational database engine written from scratch in C++. Built as a learning project to understand how databases work at the storage layer — including disk I/O, page management, B-trees, and SQL parsing.

## Features

- **SQL parser & executor** — supports `CREATE TABLE`, `INSERT`, `SELECT`, and `DELETE`
- **`WHERE` clauses** — single predicates with `=`, `!=`, `<`, `>`, `<=`, `>=`
- **`JOIN` support** — inner join across two tables with a single `ON` equality condition
- **`LIMIT`** — cap the number of rows returned by a `SELECT`
- **B-tree index** — rows are stored and searched via an on-disk B-tree
- **Buffer pool** — in-memory page cache with eviction, sitting above the pager
- **Slotted pages** — variable-length row storage within fixed-size disk pages
- **Per-table `.db` files** — each table persists to its own file (`<name>.db`)
- **Benchmarking harness** — stress-tests inserts and selects, outputs a timestamped report

## Supported SQL

```sql
-- Create a table
CREATE TABLE students (id INT, name TEXT(64), grade INT);

-- Insert rows
INSERT INTO students (id, name, grade) VALUES (1, 'Alice', 90);
INSERT INTO students VALUES (2, 'Bob', 85);   -- positional

-- Select with optional WHERE and LIMIT
SELECT * FROM students WHERE grade > 80 LIMIT 10;
SELECT id, name FROM students WHERE name = 'Alice';

-- Inner join
SELECT students.name, courses.title
FROM students JOIN courses ON students.id = courses.student_id
WHERE students.grade >= 90;
```

Column types: `INT`, `TEXT(n)`

## Project Structure

```
SQL-Mini/
├── src/
│   ├── main.cpp          # REPL entry point
│   ├── sql_engine.cpp/h  # SQL parser and query executor
│   ├── btree.cpp/h       # B-tree index (insert, search, scan)
│   ├── buffer_pool.cpp/h # Page cache and eviction
│   ├── pager.cpp/h       # Raw disk I/O (read/write pages)
│   ├── slotted_page.cpp/h# Variable-length row layout within pages
│   ├── table.cpp/h       # Table abstraction (schema + storage)
│   ├── schema.cpp/h      # Column types and schema definitions
│   └── node.cpp/h        # B-tree node serialization
├── bench/
│   ├── bench_main.cpp    # Benchmark driver
│   └── bench.sh          # Shell script to build and run benchmarks
├── Makefile
└── README.md
```

## Building

Requires a C++17-capable compiler (`g++` or `clang++`).

```bash
make          # builds tinysql (the REPL)
make bench    # builds the benchmark binary
make clean    # removes build artifacts
```

## Running

```bash
./tinysql
```

This drops you into an interactive SQL prompt. Type any supported statement and press Enter. Type `.exit` or `Ctrl+D` to quit.

Database files are created in the current directory (`<tablename>.db`).

## Benchmarks

```bash
cd bench
./bench.sh
```

Results are written to a timestamped file (`bench_YYYYMMDD_HHMMSS.txt`).

## Architecture

```
  SQL string
      │
  SQLEngine  ←── parses SQL, resolves columns, evaluates WHERE
      │
  Table  ←── owns the schema; delegates reads/writes to BTree
      │
  BTree  ←── navigates pages to find/insert rows
      │
  BufferPool  ←── in-memory page cache
      │
  Pager  ←── reads and writes raw pages to disk (.db file)
```

Each table is an independent B-tree backed by a single `.db` file. The buffer pool sits between the B-tree and the pager to reduce disk I/O.

## What's Not Implemented

- Transactions / ACID guarantees
- Indexes beyond the primary B-tree (no secondary indexes)
- `UPDATE` statement
- Multi-predicate `WHERE` (`AND` / `OR`)
- Multi-table joins (more than two tables)
- `ORDER BY` / `GROUP BY` / aggregates