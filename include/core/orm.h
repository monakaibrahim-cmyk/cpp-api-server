#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/db_driver.h>

namespace sol
{
class state;
} // namespace sol

namespace api
{

class orm_engine;
class query_builder;

/**
 * @brief Converts an English plural noun into its singular form.
 *
 * @details Handles regular and irregular inflection rules (e.g. "users" ->
 * "user", "categories" -> "category", "boxes" -> "box", "statuses" ->
 * "status").
 *
 * @param[in] word Plural word string.
 * @return std::string Singular form string.
 */
std::string singularize_word(const std::string &word);

/**
 * @brief Derives a PascalCase Model class name from a snake_case database table
 * name.
 *
 * @details Singularizes and converts underscores to CamelCase:
 * - "user_roles" -> "UserRole"
 * - "categories" -> "Category"
 * - "blog_posts" -> "BlogPost"
 *
 * @param[in] table_name Plural snake_case database table name.
 * @return std::string Singular PascalCase Model name.
 */
std::string table_to_class_name(const std::string &table_name);

/**
 * @brief Derives a singular snake_case base filename from a table name for Lua
 * models.
 *
 * @details Singularizes plural nouns:
 * - "user_roles" -> "user_role"
 * - "categories" -> "category"
 *
 * @param[in] table_name Plural snake_case database table name.
 * @return std::string Singular snake_case base filename without extension.
 */
std::string table_to_model_filename(const std::string &table_name);

/**
 * @brief Derives a plural snake_case database table name from a PascalCase
 * Model class name.
 *
 * @details Pluralizes and inserts underscores:
 * - "User" -> "users"
 * - "UserRole" -> "user_roles"
 * - "Category" -> "categories"
 *
 * @param[in] class_name Singular PascalCase Model name.
 * @return std::string Plural snake_case table name.
 */
std::string class_to_table_name(const std::string &class_name);

/**
 * @brief Fluent SQL query builder providing an expressive, chainable interface.
 *
 * @details Constructs and executes complex SQL statements with parameter
 * bindings across different relational database backends.
 *
 * Features:
 * - Projection: @ref select, @ref add_select, @ref distinct.
 * - Filtering: @ref where, @ref or_where, @ref where_in, @ref where_not_in,
 * @ref where_null,
 *   @ref where_not_null, @ref where_between, @ref where_like.
 * - Joins: @ref join, @ref left_join, @ref right_join.
 * - Sorting & Grouping: @ref order_by, @ref latest, @ref oldest, @ref group_by,
 * @ref having.
 * - Pagination: @ref limit, @ref offset.
 * - Aggregates: @ref count, @ref max, @ref min, @ref avg, @ref sum, @ref
 * exists, @ref doesnt_exist.
 * - Mutations: @ref insert, @ref insert_get_id, @ref update, @ref
 * update_or_insert,
 *   @ref del, @ref truncate, @ref increment, @ref decrement.
 *
 * Example C++ usage:
 * @code{.cpp}
 * auto users = api::s_orm().table("users")
 *     .where("status", "active")
 *     .where("age", ">=", 18)
 *     .order_by("created_at", "DESC")
 *     .limit(10)
 *     .get();
 *
 * for (const auto& user : users.rows)
 * {
 *     std::println("User: {} ({})", user.get_string("name"),
 * user.get_string("email"));
 * }
 * @endcode
 *
 * Example Lua usage:
 * @code{.lua}
 * local count = DB.table("users"):where("active", 1):count()
 * local user = DB.table("users"):where("id", 42):first()
 * @endcode
 */
class query_builder
{
  public:
    /**
     * @brief Constructs query builder targeting a specific database table.
     *
     * @param[in] table_name Target database table name.
     */
    explicit query_builder(const std::string &table_name);

    /**
     * @brief Sets the list of columns to retrieve.
     *
     * @param[in] columns Vector of column name strings.
     * @return query_builder& Fluent self reference.
     */
    query_builder &select(const std::vector<std::string> &columns);

    /**
     * @brief Sets a single column or raw SQL expression to retrieve.
     *
     * @param[in] column Column name or expression.
     * @return query_builder& Fluent self reference.
     */
    query_builder &select(const std::string &column);

    /**
     * @brief Appends an additional column to the active selection list.
     *
     * @param[in] column Column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &add_select(const std::string &column);

    /**
     * @brief Marks the query to filter duplicate rows using SELECT DISTINCT.
     *
     * @return query_builder& Fluent self reference.
     */
    query_builder &distinct();

    /**
     * @brief Adds a WHERE condition comparing column against value using a
     * specified operator.
     *
     * @param[in] column Column name.
     * @param[in] comparison_operator Operator symbol (e.g. "=", ">", "<=",
     * "<>", "LIKE").
     * @param[in] value Comparison value variant.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where(const std::string &column,
                         const std::string &comparison_operator,
                         const db_value &value);

    /**
     * @brief Adds an equality WHERE condition (column = value).
     *
     * @param[in] column Column name.
     * @param[in] value Equality comparison value.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where(const std::string &column, const db_value &value);

    /**
     * @brief Adds an OR WHERE condition with operator.
     *
     * @param[in] column Column name.
     * @param[in] comparison_operator Operator symbol.
     * @param[in] value Value variant.
     * @return query_builder& Fluent self reference.
     */
    query_builder &or_where(const std::string &column,
                            const std::string &comparison_operator,
                            const db_value &value);

    /**
     * @brief Adds an equality OR WHERE condition (column = value).
     *
     * @param[in] column Column name.
     * @param[in] value Value variant.
     * @return query_builder& Fluent self reference.
     */
    query_builder &or_where(const std::string &column, const db_value &value);

    /**
     * @brief Adds a WHERE column IN (values...) clause.
     *
     * @param[in] column Column name.
     * @param[in] values Set of values to match against.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_in(const std::string &column,
                            const std::vector<db_value> &values);

    /**
     * @brief Adds a WHERE column NOT IN (values...) clause.
     *
     * @param[in] column Column name.
     * @param[in] values Set of excluded values.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_not_in(const std::string &column,
                                const std::vector<db_value> &values);

    /**
     * @brief Adds a WHERE column IS NULL clause.
     *
     * @param[in] column Column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_null(const std::string &column);

    /**
     * @brief Adds a WHERE column IS NOT NULL clause.
     *
     * @param[in] column Column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_not_null(const std::string &column);

    /**
     * @brief Adds a WHERE column BETWEEN min AND max clause.
     *
     * @param[in] column Column name.
     * @param[in] minimum_value Lower boundary value.
     * @param[in] maximum_value Upper boundary value.
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_between(const std::string &column,
                                 const db_value &minimum_value,
                                 const db_value &maximum_value);

    /**
     * @brief Adds a WHERE column LIKE pattern clause.
     *
     * @param[in] column Column name.
     * @param[in] pattern SQL LIKE pattern string (e.g. "%user%").
     * @return query_builder& Fluent self reference.
     */
    query_builder &where_like(const std::string &column,
                              const std::string &pattern);

    /**
     * @brief Adds an arbitrary table JOIN clause.
     *
     * @param[in] table_name Target joined table.
     * @param[in] first_column Local column name.
     * @param[in] comparison_operator Operator symbol (typically "=").
     * @param[in] second_column Joined table column name.
     * @param[in] join_type SQL join type (e.g. "INNER", "LEFT", "RIGHT",
     * "CROSS").
     * @return query_builder& Fluent self reference.
     */
    query_builder &join(const std::string &table_name,
                        const std::string &first_column,
                        const std::string &comparison_operator,
                        const std::string &second_column,
                        const std::string &join_type = "INNER");

    /**
     * @brief Adds a LEFT OUTER JOIN clause.
     *
     * @param[in] table_name Target joined table.
     * @param[in] first_column Local column name.
     * @param[in] comparison_operator Operator symbol.
     * @param[in] second_column Joined table column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &left_join(const std::string &table_name,
                             const std::string &first_column,
                             const std::string &comparison_operator,
                             const std::string &second_column);

    /**
     * @brief Adds a RIGHT OUTER JOIN clause.
     *
     * @param[in] table_name Target joined table.
     * @param[in] first_column Local column name.
     * @param[in] comparison_operator Operator symbol.
     * @param[in] second_column Joined table column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &right_join(const std::string &table_name,
                              const std::string &first_column,
                              const std::string &comparison_operator,
                              const std::string &second_column);

    /**
     * @brief Specifies ordering by a column in the given direction.
     *
     * @param[in] column Column name.
     * @param[in] direction "ASC" for ascending, "DESC" for descending.
     * @return query_builder& Fluent self reference.
     */
    query_builder &order_by(const std::string &column,
                            const std::string &direction = "ASC");

    /**
     * @brief Specifies descending ordering by a column.
     *
     * @param[in] column Column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &order_by_desc(const std::string &column);

    /**
     * @brief Orders records descending by a timestamp column (defaults to
     * "created_at").
     *
     * @param[in] column Timestamp column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &latest(const std::string &column = "created_at");

    /**
     * @brief Orders records ascending by a timestamp column (defaults to
     * "created_at").
     *
     * @param[in] column Timestamp column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &oldest(const std::string &column = "created_at");

    /**
     * @brief Adds a single column to the GROUP BY clause.
     *
     * @param[in] column Column name.
     * @return query_builder& Fluent self reference.
     */
    query_builder &group_by(const std::string &column);

    /**
     * @brief Adds multiple columns to the GROUP BY clause.
     *
     * @param[in] columns List of column names.
     * @return query_builder& Fluent self reference.
     */
    query_builder &group_by(const std::vector<std::string> &columns);

    /**
     * @brief Adds an aggregate filtering HAVING clause with parameter bindings.
     *
     * @param[in] sql_expression Aggregate condition (e.g. "COUNT(*) > ?").
     * @param[in] parameters Bound values for placeholders.
     * @return query_builder& Fluent self reference.
     */
    query_builder &having(const std::string &sql_expression,
                          const std::vector<db_value> &parameters = {});

    /**
     * @brief Constrains the maximum number of rows returned by the query.
     *
     * @param[in] limit_value Maximum row limit.
     * @return query_builder& Fluent self reference.
     */
    query_builder &limit(size_t limit_value);

    /**
     * @brief Skips a specified number of rows before beginning to return
     * results.
     *
     * @param[in] offset_value Number of rows to offset.
     * @return query_builder& Fluent self reference.
     */
    query_builder &offset(size_t offset_value);

    /**
     * @brief Compiles and returns the raw SQL SELECT statement string without
     * executing it.
     *
     * @return std::string Prepared SQL query text.
     */
    std::string to_sql() const;

    /**
     * @brief Returns the vector of bound parameters associated with the
     * compiled query.
     *
     * @return std::vector<db_value> Bound parameter values.
     */
    std::vector<db_value> get_bindings() const;

    /**
     * @brief Executes the built SELECT query through the active driver and
     * returns all rows.
     *
     * @return DbResult Query result containing rows and columns.
     */
    DbResult get();

    /**
     * @brief Fetches the first matching row from the query.
     *
     * @return std::optional<DbRow> First row if available; std::nullopt
     * otherwise.
     */
    std::optional<DbRow> first();

    /**
     * @brief Finds a single record by numeric 64-bit primary key ID.
     *
     * @param[in] primary_key_id Primary key identifier.
     * @return std::optional<DbRow> Found record, or std::nullopt.
     */
    std::optional<DbRow> find(int64_t primary_key_id);

    /**
     * @brief Finds a single record by string primary key value (e.g. UUID).
     *
     * @param[in] primary_key_value String primary key.
     * @return std::optional<DbRow> Found record, or std::nullopt.
     */
    std::optional<DbRow> find(const std::string &primary_key_value);

    /**
     * @brief Executes COUNT aggregate query and returns matching record count.
     *
     * @param[in] column Target column name (defaults to "*").
     * @return int64_t Total matching rows.
     */
    int64_t count(const std::string &column = "*");

    /**
     * @brief Retrieves the single column value of the first matching record.
     *
     * @param[in] column Target column name.
     * @return std::optional<db_value> Value variant if row exists; std::nullopt
     * otherwise.
     */
    std::optional<db_value> value(const std::string &column);

    /**
     * @brief Computes the MAX aggregate of a column.
     *
     * @param[in] column Numeric column name.
     * @return double Maximum value.
     */
    double max(const std::string &column);

    /**
     * @brief Computes the MIN aggregate of a column.
     *
     * @param[in] column Numeric column name.
     * @return double Minimum value.
     */
    double min(const std::string &column);

    /**
     * @brief Computes the AVG aggregate of a column.
     *
     * @param[in] column Numeric column name.
     * @return double Average value.
     */
    double avg(const std::string &column);

    /**
     * @brief Computes the SUM aggregate of a column.
     *
     * @param[in] column Numeric column name.
     * @return double Total sum value.
     */
    double sum(const std::string &column);

    /**
     * @brief Tests if at least one matching record exists.
     *
     * @return true If count is greater than zero; false otherwise.
     */
    bool exists();

    /**
     * @brief Tests if zero matching records exist.
     *
     * @return true If count is zero; false otherwise.
     */
    bool doesnt_exist();

    /**
     * @brief Inserts a new record into the table.
     *
     * @param[in] record_values Column-to-value map to insert.
     * @return DbResult Execution result containing last_insert_id and
     * affected_rows.
     */
    DbResult
    insert(const std::unordered_map<std::string, db_value> &record_values);

    /**
     * @brief Inserts a record and returns the auto-incremented primary key ID.
     *
     * @param[in] record_values Column-to-value map.
     * @return int64_t Generated primary key ID, or -1 on error.
     */
    int64_t insert_get_id(
        const std::unordered_map<std::string, db_value> &record_values);

    /**
     * @brief Updates matching rows with new column values.
     *
     * @param[in] update_values Column-to-value map of updated attributes.
     * @return DbResult Execution result containing affected_rows count.
     */
    DbResult
    update(const std::unordered_map<std::string, db_value> &update_values);

    /**
     * @brief Updates matching rows, or inserts a new record if none match.
     *
     * @param[in] attributes Search attributes to match existing records
     * against.
     * @param[in] values Values to update or merge on insert.
     * @return DbResult Execution result.
     */
    DbResult update_or_insert(
        const std::unordered_map<std::string, db_value> &attributes,
        const std::unordered_map<std::string, db_value> &values);

    /**
     * @brief Deletes matching rows from the database.
     *
     * @return DbResult Execution result containing deleted row count.
     */
    DbResult del();

    /**
     * @brief Truncates all rows from the table.
     *
     * @return DbResult Execution result.
     */
    DbResult truncate();

    /**
     * @brief Atomically increments an integer column value by the specified
     * amount.
     *
     * @param[in] column Target integer column name.
     * @param[in] amount Increment step (defaults to 1).
     * @return DbResult Execution result.
     */
    DbResult increment(const std::string &column, int64_t amount = 1);

    /**
     * @brief Atomically decrements an integer column value by the specified
     * amount.
     *
     * @param[in] column Target integer column name.
     * @param[in] amount Decrement step (defaults to 1).
     * @return DbResult Execution result.
     */
    DbResult decrement(const std::string &column, int64_t amount = 1);

  private:
    std::string table_;
    std::string columns_ = "*";
    bool distinct_ = false;

    struct WhereClause
    {
        std::string boolean;
        std::string type;
        std::string column;
        std::string op;
        db_value value;
        std::vector<db_value> values;
        db_value min_val;
        db_value max_val;
        bool not_in = false;
        bool not_null = false;
        bool not_between = false;
    };

    struct JoinClause
    {
        std::string type;
        std::string table;
        std::string first;
        std::string op;
        std::string second;
    };

    struct OrderClause
    {
        std::string column;
        std::string direction;
    };

    struct HavingClause
    {
        std::string sql;
        std::vector<db_value> params;
    };

    using where_clause = WhereClause;
    using join_clause = JoinClause;
    using order_clause = OrderClause;
    using having_clause = HavingClause;

    std::vector<WhereClause> wheres_;
    std::vector<JoinClause> joins_;
    std::vector<OrderClause> orders_;
    std::vector<std::string> groups_;
    std::optional<HavingClause> having_;
    std::optional<size_t> limit_;
    std::optional<size_t> offset_;

    std::pair<std::string, std::vector<db_value>> build_select_sql() const;
    std::pair<std::string, std::vector<db_value>> build_wheres() const;
};

/**
 * @brief Central ORM coordinator managing database drivers, scaffolding, and
 * Lua bindings.
 *
 * @details Implements a thread-safe singleton registry allowing database client
 * libraries to register driver factories and expose an Eloquent-style query
 * builder interface to both C++ modules and Lua route scripts.
 *
 * Example C++ usage:
 * @code{.cpp}
 * api::orm_engine& orm = api::s_orm();
 * orm.register_driver_factory("mysql", []() { return
 * std::make_unique<mysql_driver>(); }); orm.set_active_driver("mysql",
 * "host=127.0.0.1;dbname=app");
 * @endcode
 */
class orm_engine
{
  public:
    /** @brief Factory callback producing fresh @ref db_driver instances. */
    using driver_factory = std::function<std::unique_ptr<db_driver>()>;

    /**
     * @brief Accesses the global singleton ORM engine instance.
     *
     * @return orm_engine& Reference to singleton instance.
     */
    static orm_engine &instance();

    /**
     * @brief Registers a database driver factory under a unique name.
     *
     * @param[in] name Driver identifier string (e.g. "mysql", "postgres").
     * @param[in] factory Creator lambda returning a unique_ptr to db_driver.
     */
    void register_driver_factory(const std::string &name,
                                 driver_factory factory);

    /**
     * @brief Checks if a driver factory is registered.
     *
     * @param[in] name Driver name.
     * @return true If registered; false otherwise.
     */
    bool has_driver_factory(const std::string &name) const;

    /**
     * @brief Returns a list of all registered driver factory names.
     *
     * @return std::vector<std::string> List of driver names.
     */
    std::vector<std::string> get_registered_drivers() const;

    /**
     * @brief Activates a registered driver and establishes database connection.
     *
     * @param[in] driver_name Registered driver identifier.
     * @param[in] connection_string Connection URI or DSN string.
     * @return true If driver was instantiated and connected successfully.
     * @return false If driver name is unknown or connection failed.
     */
    bool set_active_driver(const std::string &driver_name,
                           const std::string &connection_string = "");

    /**
     * @brief Directly sets the active driver instance.
     *
     * @param[in] driver Shared pointer to pre-instantiated driver.
     */
    void set_driver(std::shared_ptr<db_driver> driver);

    /**
     * @brief Returns a shared pointer to the currently active driver.
     *
     * @return std::shared_ptr<db_driver> Active driver pointer.
     */
    std::shared_ptr<db_driver> get_driver() const;

    /**
     * @brief Factory creating a fluent @ref query_builder targeting a database
     * table.
     *
     * @param[in] table_name Name of the database table.
     * @return query_builder New query builder instance.
     */
    query_builder table(const std::string &table_name);

    /**
     * @brief Executes a write, update, delete, or DDL command on the active
     * driver.
     *
     * @param[in] sql_query Prepared SQL text with '?' placeholders.
     * @param[in] parameters Vector of positional parameter values.
     * @return DbResult Execution result.
     */
    DbResult execute(const std::string &sql_query,
                     const std::vector<db_value> &parameters = {});

    /**
     * @brief Executes a SELECT query on the active driver.
     *
     * @param[in] sql_query Prepared SQL text.
     * @param[in] parameters Vector of positional parameter values.
     * @return DbResult Result containing rows and column metadata.
     */
    DbResult query(const std::string &sql_query,
                   const std::vector<db_value> &parameters = {});

    /**
     * @brief Creates a table based on a @ref TableSchema definition.
     *
     * @param[in] schema Schema specification.
     * @return true If created successfully; false otherwise.
     */
    bool scaffold(const TableSchema &schema);

    /**
     * @brief Tests if a table exists in the active database.
     *
     * @param[in] table_name Name of table to test.
     * @return true If table exists; false otherwise.
     */
    bool table_exists(const std::string &table_name);

    /**
     * @brief Checks if the active driver is connected.
     *
     * @return true If connected; false otherwise.
     */
    bool is_connected() const;

    /**
     * @brief Returns the name identifier of the active database driver.
     *
     * @return std::string Driver name.
     */
    std::string driver_name() const;

    /**
     * @brief Discovers all table names in the active database.
     *
     * @return std::vector<std::string> Discovered table names.
     */
    std::vector<std::string> get_tables();

    /**
     * @brief Introspects the columns and types of a table.
     *
     * @param[in] table_name Table to introspect.
     * @return std::optional<TableSchema> Table schema if found, or
     * std::nullopt.
     */
    std::optional<TableSchema> describe_table(const std::string &table_name);

    /**
     * @brief Generates Lua source code for an Active Record Model class.
     *
     * @param[in] table_name Database table name.
     * @param[in] schema_definition Optional introspected table schema.
     * @return std::string Generated Lua model code.
     */
    std::string generate_model_code(
        const std::string &table_name,
        const std::optional<TableSchema> &schema_definition = std::nullopt);

    /**
     * @brief Generates Lua source code for a Schema migration script.
     *
     * @param[in] table_name Database table name.
     * @param[in] schema_definition Optional introspected table schema.
     * @return std::string Generated Lua migration code.
     */
    std::string generate_migration_code(
        const std::string &table_name,
        const std::optional<TableSchema> &schema_definition = std::nullopt);

    /**
     * @brief Scaffolds Model and Migration Lua files on disk for a single
     * table.
     *
     * @param[in] table_name Target table name.
     * @param[in] output_models_dir Destination directory for generated models.
     * @param[in] output_migrations_dir Destination directory for generated
     * migrations.
     * @return true If both files were generated successfully; false otherwise.
     */
    bool scaffold_table_files(
        const std::string &table_name,
        const std::string &output_models_dir = "scripts/models",
        const std::string &output_migrations_dir = "scripts/migrations");

    /**
     * @brief Scaffolds Model and Migration Lua files on disk for ALL discovered
     * tables.
     *
     * @param[in] output_models_dir Destination directory for generated models.
     * @param[in] output_migrations_dir Destination directory for generated
     * migrations.
     * @return size_t Count of tables successfully scaffolded.
     */
    size_t scaffold_all_tables(
        const std::string &output_models_dir = "scripts/models",
        const std::string &output_migrations_dir = "scripts/migrations");

    /**
     * @brief Binds the Eloquent ORM facade (DB.table, DB.query, DB.execute,
     * Model) to Lua.
     *
     * @param[in,out] lua_state Reference to Sol2 state.
     */
    void bind_lua(sol::state &lua_state);

  private:
    orm_engine();
    ~orm_engine();

    mutable std::mutex mutex_;
    std::unordered_map<std::string, driver_factory> factories_;
    std::shared_ptr<db_driver> active_driver_;
};

/**
 * @brief Returns global singleton instance of orm_engine.
 *
 * @return orm_engine& Reference to global ORM engine.
 */
inline orm_engine &s_orm() { return orm_engine::instance(); }

} // namespace api
