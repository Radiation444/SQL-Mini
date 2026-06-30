#include <iostream>
#include <string>
#include <cstdlib>
#include "sql_engine.h"

static void demo(SQLEngine& db) {
    std::cout << "=== Demo ===\n\n";

    std::cout << "> " << "CREATE TABLE users (id INT, name TEXT(64), age INT);\n";
    std::cout << db.exec("CREATE TABLE users (id INT, name TEXT(64), age INT)") << "\n\n";

    struct { int id; const char* name; int age; } people[] = {
        {1, "Alice", 30}, {2, "Bob", 25}, {3, "Carol", 35},
        {4, "Dave",  28}, {5, "Eve",  22}
    };
    for (auto& p : people) {
        std::string sql = "INSERT INTO users VALUES (" +
                          std::to_string(p.id) + ", '" + p.name + "', " +
                          std::to_string(p.age) + ")";
        std::cout << "> " << sql << ";\n";
        std::cout << db.exec(sql) << "\n";
    }
    std::cout << "\n";

    auto run = [&](const std::string& sql) {
        std::cout << "> " << sql << ";\n";
        std::cout << db.exec(sql) << "\n";
    };

    run("SELECT * FROM users");
    run("SELECT name, age FROM users WHERE age > 25");
    run("SELECT name FROM users WHERE name = 'Eve'");
    run("SELECT * FROM users LIMIT 3");
}

int main(int argc, char* argv[]) {
    SQLEngine db;

    if (argc > 1 && std::string(argv[1]) == "--demo") {
        // Demo runs in a throw-away file so it always starts fresh.
        std::remove("users.db");
        demo(db);
        std::remove("users.db");
        return 0;
    }

    // Interactive REPL
    std::cout << "TinySQL  (type 'exit' or Ctrl-D to quit)\n";
    std::cout << "Supported: CREATE TABLE, INSERT INTO, SELECT ... FROM ... [WHERE ...] [LIMIT n]\n\n";

    std::string line, buf;
    while (true) {
        std::cout << (buf.empty() ? "sql> " : "   > ") << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        buf += " " + line;
        if (buf.find(';') != std::string::npos || (!buf.empty() && line.empty())) {
            std::cout << db.exec(buf) << "\n";
            buf.clear();
        }
    }
    return 0;
}