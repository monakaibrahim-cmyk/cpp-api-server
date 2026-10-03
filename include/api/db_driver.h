#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace api
{

/**
 * @brief Universal polymorphic database value representation.
 *
 * @details Represents SQL null, 64-bit signed integer, double-precision float,
 * UTF-8 text string, boolean flag, or binary byte array (BLOB). Used across all
 * driver query interfaces and ORM query builder parameter bindings.
 */
using db_value = std::variant<std::nullptr_t, int64_t, double, std::string,
                              bool, std::vector<uint8_t>>;

/**
 * @brief Formats any @ref db_value variant into a human-readable text
 * representation.
 *
 * @details Converts types as follows:
 * - std::nullptr_t: "NULL"
 * - int64_t: base-10 integer string
 * - double: floating-point string
 * - std::string: string contents
 * - bool: "true" or "false"
 * - std::vector<uint8_t>: "[BLOB N bytes]"
 *
 * @param[in] value Database value variant.
 * @return std::string Formatted representation.
 */
inline std::string db_value_to_string(const db_value &value)
{
    return std::visit(
        [](auto &&argument) -> std::string
        {
            using TargetType = std::decay_t<decltype(argument)>;

            if constexpr (std::is_same_v<TargetType, std::nullptr_t>)
            {
                return "NULL";
            }
            else if constexpr (std::is_same_v<TargetType, int64_t>)
            {
                return std::to_string(argument);
            }
            else if constexpr (std::is_same_v<TargetType, double>)
            {
                return std::to_string(argument);
            }
            else if constexpr (std::is_same_v<TargetType, std::string>)
            {
                return argument;
            }
            else if constexpr (std::is_same_v<TargetType, bool>)
            {
                return argument ? "true" : "false";
            }
            else if constexpr (std::is_same_v<TargetType, std::vector<uint8_t>>)
            {
                return "[BLOB " + std::to_string(argument.size()) + " bytes]";
            }

            return "";
        },
        value);
}

/**
 * @brief Represents a single tabular record returned by a database query.
 *
 * @details Retains both positional (zero-based index) and associative (column
 * name) access to fields with type coercion helpers (@ref get_string, @ref
 * get_int, @ref get_double, @ref get_bool).
 *
 * Example:
 * @code{.cpp}
 * api::DbRow row;
 * row.add_column("id", int64_t(42));
 * row.add_column("username", std::string("alice"));
 *
 * int64_t user_id = row.get_int("id");
 * std::string name = row.get_string("username");
 * @endcode
 */
struct DbRow
{
    /** @brief Ordered list of column names matching the values array. */
    std::vector<std::string> column_names;

    /** @brief Ordered column values. */
    std::vector<db_value> values;

    /** @brief Fast hash index resolving column names to array positions. */
    std::unordered_map<std::string, size_t> column_index_map;

    /**
     * @brief Appends a column name and value pair to the row.
     *
     * @param[in] column_name Name of the column.
     * @param[in] column_value Value variant for the column.
     */
    void add_column(std::string column_name, db_value column_value)
    {
        column_index_map[column_name] = values.size();
        column_names.push_back(std::move(column_name));
        values.push_back(std::move(column_value));
    }

    /**
     * @brief Returns the total number of columns in the row.
     *
     * @return size_t Column count.
     */
    size_t size() const { return values.size(); }

    /**
     * @brief Checks if a column exists by name.
     *
     * @param[in] column_name Column name to check.
     * @return true If column exists in the row; false otherwise.
     */
    bool has(const std::string &column_name) const
    {
        return column_index_map.find(column_name) != column_index_map.end();
    }

    /**
     * @brief Retrieves the raw @ref db_value by zero-based index.
     *
     * @param[in] index Column index.
     * @return const db_value& Reference to value, or static null if out of
     * range.
     */
    const db_value &get(size_t index) const
    {
        static const db_value s_null = nullptr;

        if (index < values.size())
        {
            return values[index];
        }

        return s_null;
    }

    /**
     * @brief Retrieves the raw @ref db_value by column name.
     *
     * @param[in] column_name Column name.
     * @return const db_value& Reference to value, or static null if not found.
     */
    const db_value &get(const std::string &column_name) const
    {
        static const db_value s_null = nullptr;
        auto iterator = column_index_map.find(column_name);

        if (iterator != column_index_map.end())
        {
            return values[iterator->second];
        }

        return s_null;
    }

    /**
     * @brief Retrieves string value by column name with optional fallback
     * default.
     *
     * @param[in] column_name Name of the column.
     * @param[in] default_value Fallback value returned if column is null or
     * missing.
     * @return std::string String value or fallback.
     */
    std::string get_string(const std::string &column_name,
                           const std::string &default_value = "") const
    {
        auto iterator = column_index_map.find(column_name);

        if (iterator == column_index_map.end())
        {
            return default_value;
        }

        const auto &raw_value = values[iterator->second];

        if (std::holds_alternative<std::string>(raw_value))
        {
            return std::get<std::string>(raw_value);
        }

        if (std::holds_alternative<std::nullptr_t>(raw_value))
        {
            return default_value;
        }

        return db_value_to_string(raw_value);
    }

    /**
     * @brief Retrieves string value by column index with optional fallback
     * default.
     *
     * @param[in] index Zero-based column index.
     * @param[in] default_value Fallback value returned if column is null or
     * invalid.
     * @return std::string String value or fallback.
     */
    std::string get_string(size_t index,
                           const std::string &default_value = "") const
    {
        if (index >= values.size())
        {
            return default_value;
        }

        const auto &raw_value = values[index];

        if (std::holds_alternative<std::string>(raw_value))
        {
            return std::get<std::string>(raw_value);
        }

        if (std::holds_alternative<std::nullptr_t>(raw_value))
        {
            return default_value;
        }

        return db_value_to_string(raw_value);
    }

    /**
     * @brief Retrieves 64-bit integer by column name with automatic type
     * conversion.
     *
     * @param[in] column_name Name of the column.
     * @param[in] default_value Fallback value returned on missing column or
     * conversion failure.
     * @return int64_t Integer representation.
     */
    int64_t get_int(const std::string &column_name,
                    int64_t default_value = 0) const
    {
        auto iterator = column_index_map.find(column_name);

        if (iterator == column_index_map.end())
        {
            return default_value;
        }

        const auto &raw_value = values[iterator->second];

        if (std::holds_alternative<int64_t>(raw_value))
        {
            return std::get<int64_t>(raw_value);
        }

        if (std::holds_alternative<double>(raw_value))
        {
            return static_cast<int64_t>(std::get<double>(raw_value));
        }

        if (std::holds_alternative<bool>(raw_value))
        {
            return std::get<bool>(raw_value) ? 1 : 0;
        }

        if (std::holds_alternative<std::string>(raw_value))
        {
            try
            {
                return std::stoll(std::get<std::string>(raw_value));
            }
            catch (...)
            {
                return default_value;
            }
        }

        return default_value;
    }

    /**
     * @brief Retrieves 64-bit integer by zero-based index.
     *
     * @param[in] index Column index.
     * @param[in] default_value Fallback value.
     * @return int64_t Integer value.
     */
    int64_t get_int(size_t index, int64_t default_value = 0) const
    {
        if (index >= values.size())
        {
            return default_value;
        }

        const auto &raw_value = values[index];

        if (std::holds_alternative<int64_t>(raw_value))
        {
            return std::get<int64_t>(raw_value);
        }

        if (std::holds_alternative<double>(raw_value))
        {
            return static_cast<int64_t>(std::get<double>(raw_value));
        }

        if (std::holds_alternative<bool>(raw_value))
        {
            return std::get<bool>(raw_value) ? 1 : 0;
        }

        if (std::holds_alternative<std::string>(raw_value))
        {
            try
            {
                return std::stoll(std::get<std::string>(raw_value));
            }
            catch (...)
            {
                return default_value;
            }
        }

        return default_value;
    }

    /**
     * @brief Retrieves floating point double by column name.
     *
     * @param[in] column_name Name of the column.
     * @param[in] default_value Fallback value.
     * @return double Floating point value.
     */
    double get_double(const std::string &column_name,
                      double default_value = 0.0) const
    {
        auto iterator = column_index_map.find(column_name);

        if (iterator == column_index_map.end())
        {
            return default_value;
        }

        const auto &raw_value = values[iterator->second];

        if (std::holds_alternative<double>(raw_value))
        {
            return std::get<double>(raw_value);
        }

        if (std::holds_alternative<int64_t>(raw_value))
        {
            return static_cast<double>(std::get<int64_t>(raw_value));
        }

        if (std::holds_alternative<std::string>(raw_value))
        {
            try
            {
                return std::stod(std::get<std::string>(raw_value));
            }
            catch (...)
            {
                return default_value;
            }
        }

        return default_value;
    }

    /**
     * @brief Retrieves boolean by column name.
     *
     * @param[in] column_name Name of the column.
     * @param[in] default_value Fallback value.
     * @return true If value evaluates to truthy; false otherwise.
     */
    bool get_bool(const std::string &column_name,
                  bool default_value = false) const
    {
        auto iterator = column_index_map.find(column_name);

        if (iterator == column_index_map.end())
        {
            return default_value;
        }

        const auto &raw_value = values[iterator->second];

        if (std::holds_alternative<bool>(raw_value))
        {
            return std::get<bool>(raw_value);
        }

        if (std::holds_alternative<int64_t>(raw_value))
        {
            return std::get<int64_t>(raw_value) != 0;
        }

        if (std::holds_alternative<std::string>(raw_value))
        {
            const std::string &string_value = std::get<std::string>(raw_value);

            return (string_value == "1" || string_value == "true" ||
                    string_value == "TRUE");
        }

        return default_value;
    }
};

/// Backward compatibility alias
using db_row = DbRow;

/**
 * @brief Structured result of a database query or DDL execution.
 *
 * @details Carries execution success/failure flags, error diagnostics,
 * modified row counts, last generated primary keys, and retrieved rows.
 */
struct DbResult
{
    /** @brief Whether the SQL query executed successfully without errors. */
    bool success = false;

    /** @brief Error diagnostic message if execution failed. */
    std::string error_message;

    /** @brief Number of rows affected by an INSERT, UPDATE, or DELETE
     * statement. */
    int64_t affected_rows = 0;

    /** @brief Auto-incremented primary key generated by the last INSERT
     * statement. */
    int64_t last_insert_id = 0;

    /** @brief Schema column names returned in the result set. */
    std::vector<std::string> column_names;

    /** @brief Set of rows returned by a SELECT query. */
    std::vector<DbRow> rows;

    /**
     * @brief Returns the total row count in the result set.
     *
     * @return size_t Number of rows.
     */
    size_t row_count() const { return rows.size(); }

    /**
     * @brief Checks if the result set contains zero rows.
     *
     * @return true If empty; false otherwise.
     */
    bool empty() const { return rows.empty(); }

    /**
     * @brief Returns a reference to the first row in the result set.
     *
     * @return const DbRow& Reference to first row, or an empty row if result
     * set is empty.
     */
    const DbRow &front() const
    {
        static const DbRow s_empty{};

        if (!rows.empty())
        {
            return rows.front();
        }

        return s_empty;
    }
};

/// Backward compatibility alias
using db_result = DbResult;

/**
 * @brief Column schema definition used in database table scaffolding and
 * migrations.
 */
struct ColumnDef
{
    /** @brief Column field name. */
    std::string name;

    /** @brief SQL column type string (e.g. "integer", "varchar(255)", "text",
     * "datetime"). */
    std::string type = "text";

    /** @brief Whether this column participates in the primary key. */
    bool primary_key = false;

    /** @brief Whether this column auto-increments upon insertion. */
    bool auto_increment = false;

    /** @brief Whether the column has a NOT NULL constraint. */
    bool not_null = false;

    /** @brief Whether the column has a UNIQUE index constraint. */
    bool unique = false;

    /** @brief Default literal value string (e.g. "0", "CURRENT_TIMESTAMP"). */
    std::string default_value;
};

/// Backward compatibility alias
using column_def = ColumnDef;

/**
 * @brief Complete database table schema specification.
 */
struct TableSchema
{
    /** @brief Table name. */
    std::string table_name;

    /** @brief List of column definitions defining the schema. */
    std::vector<ColumnDef> columns;

    /** @brief Emits "IF NOT EXISTS" in generated DDL statements when true. */
    bool if_not_exists = true;
};

/// Backward compatibility alias
using table_schema = TableSchema;

/**
 * @brief Abstract driver interface for pluggable database engines.
 *
 * @details Modular C++ plugins implement this interface to connect the API
 * server to real relational database backends (e.g. MySQL, PostgreSQL, SQLite).
 *
 * Example implementation:
 * @code{.cpp}
 * class my_custom_driver : public api::db_driver
 * {
 * public:
 *     const std::string& driver_name() const override { static std::string name
 * = "custom"; return name; } bool connect(const std::string& conn_str) override
 * { return true; } void disconnect() override {} bool is_connected() const
 * override { return true; } api::DbResult execute(const std::string& sql, const
 * std::vector<api::db_value>& params) override
 *     {
 *         api::DbResult res; res.success = true; return res;
 *     }
 *     api::DbResult query(const std::string& sql, const
 * std::vector<api::db_value>& params) override
 *     {
 *         api::DbResult res; res.success = true; return res;
 *     }
 *     bool table_exists(const std::string& table) override { return true; }
 *     bool scaffold_table(const api::TableSchema& schema) override { return
 * true; } std::string get_last_error() const override { return ""; }
 * };
 * @endcode
 */
class db_driver
{
  public:
    /** @brief Virtual destructor for clean interface cleanup. */
    virtual ~db_driver() = default;

    /**
     * @brief Returns the identifier name of this database driver (e.g. "mysql",
     * "postgres").
     *
     * @return const std::string& Driver name identifier.
     */
    virtual const std::string &driver_name() const = 0;

    /**
     * @brief Establishes connection to the target database server.
     *
     * @param[in] connection_string DSN or URI containing connection parameters.
     * @return true If connected successfully; false otherwise.
     */
    virtual bool connect(const std::string &connection_string) = 0;

    /**
     * @brief Closes all open connections and frees client library handles.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Verifies whether the database connection is currently alive and
     * active.
     *
     * @return true If ready for queries; false otherwise.
     */
    virtual bool is_connected() const = 0;

    /**
     * @brief Executes a write, update, delete, or DDL command.
     *
     * @param[in] sql_query Prepared SQL statement text with '?' parameter
     * placeholders.
     * @param[in] parameters Vector of positional parameter values bound to
     * placeholders.
     * @return DbResult Result containing affected row count, last insert ID,
     * and error messages.
     */
    virtual DbResult execute(const std::string &sql_query,
                             const std::vector<db_value> &parameters = {}) = 0;

    /**
     * @brief Executes a SELECT query returning a tabular result set.
     *
     * @param[in] sql_query SQL query text with '?' placeholders.
     * @param[in] parameters Positional parameter bindings.
     * @return DbResult Tabular rows and column metadata.
     */
    virtual DbResult query(const std::string &sql_query,
                           const std::vector<db_value> &parameters = {}) = 0;

    /**
     * @brief Tests if a table exists in the connected database catalog.
     *
     * @param[in] table_name Name of the table.
     * @return true If table exists; false otherwise.
     */
    virtual bool table_exists(const std::string &table_name) = 0;

    /**
     * @brief Scaffolds and creates a table based on a @ref TableSchema
     * definition.
     *
     * @param[in] schema Schema containing table and column specifications.
     * @return true If table was created successfully; false otherwise.
     */
    virtual bool scaffold_table(const TableSchema &schema) = 0;

    /**
     * @brief Introspects the database catalog to retrieve all existing user
     * table names.
     *
     * @return std::vector<std::string> List of discovered table names.
     */
    virtual std::vector<std::string> get_tables() { return {}; }

    /**
     * @brief Introspects the schema and columns of a specific database table.
     *
     * @param[in] table_name Name of table to inspect.
     * @return std::optional<TableSchema> Table schema if found, or
     * std::nullopt.
     */
    virtual std::optional<TableSchema>
    describe_table(const std::string &table_name)
    {
        return std::nullopt;
    }

    /**
     * @brief Returns the last diagnostic error message reported by the driver.
     *
     * @return std::string Error message string.
     */
    virtual std::string get_last_error() const = 0;
};

/**
 * @brief Null object driver used when no database driver is configured.
 *
 * @details Gracefully handles query attempts by returning failed @ref DbResult
 * objects with actionable instructions rather than throwing exceptions or
 * crashing.
 */
class null_db_driver : public db_driver
{
  public:
    const std::string &driver_name() const override
    {
        static const std::string s_name = "none";

        return s_name;
    }

    bool connect(const std::string & /*connection_string*/) override
    {
        return false;
    }

    void disconnect() override {}

    bool is_connected() const override { return false; }

    DbResult execute(const std::string & /*sql_query*/,
                     const std::vector<db_value> & /*parameters*/ = {}) override
    {
        DbResult result;

        result.success = false;
        result.error_message = "No database driver configured. Set db.driver "
                               "in server.lua or register a C++ module.";

        return result;
    }

    DbResult query(const std::string & /*sql_query*/,
                   const std::vector<db_value> & /*parameters*/ = {}) override
    {
        DbResult result;

        result.success = false;
        result.error_message = "No database driver configured. Set db.driver "
                               "in server.lua or register a C++ module.";

        return result;
    }

    bool table_exists(const std::string & /*table_name*/) override
    {
        return false;
    }

    bool scaffold_table(const TableSchema & /*schema*/) override
    {
        return false;
    }

    std::string get_last_error() const override
    {
        return "No database driver configured.";
    }
};

} // namespace api
