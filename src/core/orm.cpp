#include <core/orm.h>
#include <core/logger.h>
#include <globals.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <sol/sol.hpp>

namespace api
{

namespace
{

db_value sol_to_db_value(const sol::object &obj)
{
    if (!obj.valid() || obj.is<sol::nil_t>())
    {
        return nullptr;
    }

    if (obj.is<bool>())
    {
        return obj.as<bool>();
    }

    if (obj.is<int64_t>())
    {
        return obj.as<int64_t>();
    }

    if (obj.is<double>())
    {
        return obj.as<double>();
    }

    if (obj.is<std::string>())
    {
        return obj.as<std::string>();
    }

    return db_value(nullptr);
}

sol::object db_value_to_sol(sol::state_view lua, const db_value &val)
{
    return std::visit(
        [&lua](auto &&arg) -> sol::object
        {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::nullptr_t>)
            {
                return sol::nil;
            }
            else if constexpr (std::is_same_v<T, int64_t>)
            {
                return sol::make_object(lua, arg);
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return sol::make_object(lua, arg);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return sol::make_object(lua, arg);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return sol::make_object(lua, arg);
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                std::string s(reinterpret_cast<const char *>(arg.data()),
                              arg.size());

                return sol::make_object(lua, s);
            }

            return sol::nil;
        },
        val);
}

void parse_args(const sol::variadic_args &args, std::string &out_sql,
                std::vector<db_value> &out_params)
{
    size_t start_idx = 0;

    if (args.size() > 0)
    {
        sol::object first = args[0];

        if (first.is<sol::table>())
        {
            sol::table t = first.as<sol::table>();

            if (t["execute"].valid())
            {
                start_idx = 1;
            }
        }
    }

    if (start_idx >= args.size())
    {
        return;
    }

    out_sql = args[start_idx].as<std::string>();

    if (start_idx + 1 < args.size())
    {
        sol::object param_obj = args[start_idx + 1];

        if (param_obj.is<sol::table>())
        {
            sol::table pt = param_obj.as<sol::table>();
            size_t len = pt.size();

            for (size_t i = 1; i <= len; ++i)
            {
                out_params.push_back(sol_to_db_value(pt[i]));
            }
        }
        else
        {
            for (size_t i = start_idx + 1; i < args.size(); ++i)
            {
                out_params.push_back(sol_to_db_value(args[i]));
            }
        }
    }
}

} // namespace

orm_engine &orm_engine::instance()
{
    static orm_engine s_instance;

    return s_instance;
}

orm_engine::orm_engine() : active_driver_(std::make_shared<null_db_driver>()) {}

orm_engine::~orm_engine() = default;

void orm_engine::register_driver_factory(const std::string &name,
                                         driver_factory factory)
{
    std::lock_guard<std::mutex> lock(mutex_);

    factories_[name] = std::move(factory);

    LOG_INFO("orm", "Registered DB driver factory: " << name);
}

bool orm_engine::has_driver_factory(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return factories_.find(name) != factories_.end();
}

std::vector<std::string> orm_engine::get_registered_drivers() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;

    names.reserve(factories_.size());

    for (const auto &pair : factories_)
    {
        names.push_back(pair.first);
    }

    return names;
}

bool orm_engine::set_active_driver(const std::string &driver_name,
                                   const std::string &connection_string)
{
    driver_factory factory = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = factories_.find(driver_name);

        if (it != factories_.end())
        {
            factory = it->second;
        }
    }

    if (!factory)
    {
        LOG_ERROR("orm", "Cannot activate DB driver: " << driver_name
                                                       << " (not registered)");

        return false;
    }

    auto driver = factory();

    if (!driver)
    {
        LOG_ERROR("orm",
                  "DB driver factory for " << driver_name << " returned null");

        return false;
    }

    if (!driver->connect(connection_string))
    {
        LOG_ERROR("orm", "Failed to connect DB driver "
                             << driver_name
                             << " with connection: " << connection_string
                             << " (" << driver->get_last_error() << ")");

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        active_driver_ = std::move(driver);
    }

    LOG_INFO("orm", "Active DB driver set to: " << driver_name << " ("
                                                << connection_string << ")");

    return true;
}

void orm_engine::set_driver(std::shared_ptr<db_driver> driver)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (driver)
    {
        active_driver_ = std::move(driver);
    }
    else
    {
        active_driver_ = std::make_shared<null_db_driver>();
    }
}

std::shared_ptr<db_driver> orm_engine::get_driver() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return active_driver_;
}

query_builder::query_builder(const std::string &table_name) : table_(table_name)
{
}

query_builder &query_builder::select(const std::vector<std::string> &columns)
{
    if (columns.empty())
    {
        columns_ = "*";

        return *this;
    }

    std::string s;

    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i > 0)
        {
            s += ", ";
        }

        s += columns[i];
    }

    columns_ = s;

    return *this;
}

query_builder &query_builder::select(const std::string &column)
{
    columns_ = column;

    return *this;
}

query_builder &query_builder::add_select(const std::string &column)
{
    if (columns_ == "*")
    {
        columns_ = column;
    }
    else
    {
        columns_ += ", " + column;
    }

    return *this;
}

query_builder &query_builder::distinct()
{
    distinct_ = true;

    return *this;
}

query_builder &query_builder::where(const std::string &column,
                                    const std::string &op,
                                    const db_value &value)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "basic";
    w.column = column;
    w.op = op;
    w.value = value;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where(const std::string &column,
                                    const db_value &value)
{
    return where(column, "=", value);
}

query_builder &query_builder::or_where(const std::string &column,
                                       const std::string &op,
                                       const db_value &value)
{
    where_clause w;

    w.boolean = "OR";
    w.type = "basic";
    w.column = column;
    w.op = op;
    w.value = value;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::or_where(const std::string &column,
                                       const db_value &value)
{
    return or_where(column, "=", value);
}

query_builder &query_builder::where_in(const std::string &column,
                                       const std::vector<db_value> &values)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "in";
    w.column = column;
    w.values = values;
    w.not_in = false;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where_not_in(const std::string &column,
                                           const std::vector<db_value> &values)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "in";
    w.column = column;
    w.values = values;
    w.not_in = true;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where_null(const std::string &column)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "null";
    w.column = column;
    w.not_null = false;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where_not_null(const std::string &column)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "null";
    w.column = column;
    w.not_null = true;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where_between(const std::string &column,
                                            const db_value &min_val,
                                            const db_value &max_val)
{
    where_clause w;

    w.boolean = "AND";
    w.type = "between";
    w.column = column;
    w.min_val = min_val;
    w.max_val = max_val;
    w.not_between = false;

    wheres_.push_back(std::move(w));

    return *this;
}

query_builder &query_builder::where_like(const std::string &column,
                                         const std::string &pattern)
{
    return where(column, "LIKE", pattern);
}

query_builder &query_builder::join(const std::string &table,
                                   const std::string &first,
                                   const std::string &op,
                                   const std::string &second,
                                   const std::string &type)
{
    join_clause j;

    j.type = type;
    j.table = table;
    j.first = first;
    j.op = op;
    j.second = second;

    joins_.push_back(std::move(j));

    return *this;
}

query_builder &query_builder::left_join(const std::string &table,
                                        const std::string &first,
                                        const std::string &op,
                                        const std::string &second)
{
    return join(table, first, op, second, "LEFT");
}

query_builder &query_builder::right_join(const std::string &table,
                                         const std::string &first,
                                         const std::string &op,
                                         const std::string &second)
{
    return join(table, first, op, second, "RIGHT");
}

query_builder &query_builder::order_by(const std::string &column,
                                       const std::string &direction)
{
    order_clause o;

    o.column = column;
    o.direction = direction;

    orders_.push_back(std::move(o));

    return *this;
}

query_builder &query_builder::order_by_desc(const std::string &column)
{
    return order_by(column, "DESC");
}

query_builder &query_builder::latest(const std::string &column)
{
    return order_by(column, "DESC");
}

query_builder &query_builder::oldest(const std::string &column)
{
    return order_by(column, "ASC");
}

query_builder &query_builder::group_by(const std::string &column)
{
    groups_.push_back(column);

    return *this;
}

query_builder &query_builder::group_by(const std::vector<std::string> &columns)
{
    for (const auto &c : columns)
    {
        groups_.push_back(c);
    }

    return *this;
}

query_builder &query_builder::having(const std::string &sql,
                                     const std::vector<db_value> &params)
{
    having_clause h;

    h.sql = sql;
    h.params = params;

    having_ = std::move(h);

    return *this;
}

query_builder &query_builder::limit(size_t n)
{
    limit_ = n;

    return *this;
}

query_builder &query_builder::offset(size_t n)
{
    offset_ = n;

    return *this;
}

std::pair<std::string, std::vector<db_value>>
query_builder::build_wheres() const
{
    if (wheres_.empty())
    {
        return {"", {}};
    }

    std::string sql = " WHERE ";
    std::vector<db_value> bindings;

    for (size_t i = 0; i < wheres_.size(); ++i)
    {
        const auto &w = wheres_[i];

        if (i > 0)
        {
            sql += " " + w.boolean + " ";
        }

        if (w.type == "basic")
        {
            sql += w.column + " " + w.op + " ?";
            bindings.push_back(w.value);
        }
        else if (w.type == "in")
        {
            sql += w.column + (w.not_in ? " NOT IN (" : " IN (");

            for (size_t k = 0; k < w.values.size(); ++k)
            {
                if (k > 0)
                {
                    sql += ", ";
                }

                sql += "?";
                bindings.push_back(w.values[k]);
            }

            sql += ")";
        }
        else if (w.type == "null")
        {
            sql += w.column + (w.not_null ? " IS NOT NULL" : " IS NULL");
        }
        else if (w.type == "between")
        {
            sql += w.column + (w.not_between ? " NOT BETWEEN ? AND ?"
                                             : " BETWEEN ? AND ?");
            bindings.push_back(w.min_val);
            bindings.push_back(w.max_val);
        }
    }

    return {sql, bindings};
}

std::pair<std::string, std::vector<db_value>>
query_builder::build_select_sql() const
{
    std::string sql = "SELECT ";

    if (distinct_)
    {
        sql += "DISTINCT ";
    }

    sql += columns_ + " FROM " + table_;

    std::vector<db_value> bindings;

    for (const auto &j : joins_)
    {
        sql += " " + j.type + " JOIN " + j.table + " ON " + j.first + " " +
               j.op + " " + j.second;
    }

    auto wheres_res = build_wheres();

    sql += wheres_res.first;

    for (const auto &b : wheres_res.second)
    {
        bindings.push_back(b);
    }

    if (!groups_.empty())
    {
        sql += " GROUP BY ";

        for (size_t i = 0; i < groups_.size(); ++i)
        {
            if (i > 0)
            {
                sql += ", ";
            }

            sql += groups_[i];
        }
    }

    if (having_.has_value())
    {
        sql += " HAVING " + having_->sql;

        for (const auto &p : having_->params)
        {
            bindings.push_back(p);
        }
    }

    if (!orders_.empty())
    {
        sql += " ORDER BY ";

        for (size_t i = 0; i < orders_.size(); ++i)
        {
            if (i > 0)
            {
                sql += ", ";
            }

            sql += orders_[i].column + " " + orders_[i].direction;
        }
    }

    if (limit_.has_value())
    {
        sql += " LIMIT " + std::to_string(limit_.value());
    }

    if (offset_.has_value())
    {
        sql += " OFFSET " + std::to_string(offset_.value());
    }

    return {sql, bindings};
}

std::string query_builder::to_sql() const { return build_select_sql().first; }

std::vector<db_value> query_builder::get_bindings() const
{
    return build_select_sql().second;
}

db_result query_builder::get()
{
    auto [sql, bindings] = build_select_sql();

    return s_orm().query(sql, bindings);
}

std::optional<db_row> query_builder::first()
{
    query_builder q = *this;

    q.limit(1);

    db_result res = q.get();

    if (res.success && !res.rows.empty())
    {
        return res.rows.front();
    }

    return std::nullopt;
}

int64_t query_builder::count(const std::string &column)
{
    query_builder q = *this;

    q.columns_ = "COUNT(" + column + ") AS agg_count";

    db_result res = q.get();

    if (res.success && !res.rows.empty())
    {
        return res.rows.front().get_int("agg_count", 0);
    }

    return 0;
}

bool query_builder::exists() { return count() > 0; }

bool query_builder::doesnt_exist() { return !exists(); }

std::optional<db_value> query_builder::value(const std::string &column)
{
    auto row = first();

    if (row.has_value() && row->has(column))
    {
        size_t idx = 0;

        for (size_t i = 0; i < row->column_names.size(); ++i)
        {
            if (row->column_names[i] == column)
            {
                idx = i;
                break;
            }
        }

        return row->values[idx];
    }

    return std::nullopt;
}

db_result
query_builder::insert(const std::unordered_map<std::string, db_value> &data)
{
    if (data.empty())
    {
        db_result res;

        res.success = false;
        res.error_message = "Insert data cannot be empty";

        return res;
    }

    std::string sql = "INSERT INTO " + table_ + " (";
    std::string placeholders;
    std::vector<db_value> params;
    bool first = true;

    for (const auto &[k, v] : data)
    {
        if (!first)
        {
            sql += ", ";
            placeholders += ", ";
        }

        sql += k;
        placeholders += "?";
        params.push_back(v);
        first = false;
    }

    sql += R"sql() VALUES ()sql" + placeholders + ")";

    return s_orm().execute(sql, params);
}

int64_t query_builder::insert_get_id(
    const std::unordered_map<std::string, db_value> &data)
{
    db_result res = insert(data);

    if (res.success)
    {
        return res.last_insert_id;
    }

    return -1;
}

db_result
query_builder::update(const std::unordered_map<std::string, db_value> &data)
{
    if (data.empty())
    {
        db_result res;

        res.success = false;
        res.error_message = "Update data cannot be empty";

        return res;
    }

    std::string sql = "UPDATE " + table_ + " SET ";
    std::vector<db_value> params;
    bool first = true;

    for (const auto &[k, v] : data)
    {
        if (!first)
        {
            sql += ", ";
        }

        sql += k + " = ?";
        params.push_back(v);
        first = false;
    }

    auto [where_sql, where_params] = build_wheres();

    sql += where_sql;

    for (const auto &p : where_params)
    {
        params.push_back(p);
    }

    return s_orm().execute(sql, params);
}

db_result query_builder::update_or_insert(
    const std::unordered_map<std::string, db_value> &attributes,
    const std::unordered_map<std::string, db_value> &values)
{
    query_builder q = *this;

    for (const auto &[k, v] : attributes)
    {
        q.where(k, v);
    }

    if (q.exists())
    {
        if (!values.empty())
        {
            return q.update(values);
        }

        db_result res;

        res.success = true;
        res.affected_rows = 0;

        return res;
    }

    std::unordered_map<std::string, db_value> merged = attributes;

    for (const auto &[k, v] : values)
    {
        merged[k] = v;
    }

    return insert(merged);
}

db_result query_builder::del()
{
    auto [where_sql, params] = build_wheres();
    std::string sql = "DELETE FROM " + table_ + where_sql;

    return s_orm().execute(sql, params);
}

db_result query_builder::truncate()
{
    std::string sql = "DELETE FROM " + table_;

    return s_orm().execute(sql);
}

db_result query_builder::increment(const std::string &column, int64_t amount)
{
    auto [where_sql, where_params] = build_wheres();
    std::string sql = "UPDATE " + table_ + " SET " + column + " = " + column +
                      " + ?" + where_sql;
    std::vector<db_value> params;

    params.push_back(amount);

    for (const auto &p : where_params)
    {
        params.push_back(p);
    }

    return s_orm().execute(sql, params);
}

db_result query_builder::decrement(const std::string &column, int64_t amount)
{
    auto [where_sql, where_params] = build_wheres();
    std::string sql = "UPDATE " + table_ + " SET " + column + " = " + column +
                      " - ?" + where_sql;
    std::vector<db_value> params;

    params.push_back(amount);

    for (const auto &p : where_params)
    {
        params.push_back(p);
    }

    return s_orm().execute(sql, params);
}

query_builder orm_engine::table(const std::string &table_name)
{
    return query_builder(table_name);
}

db_result orm_engine::execute(const std::string &sql,
                              const std::vector<db_value> &params)
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        db_result res;

        res.success = false;
        res.error_message = "No database driver available.";

        return res;
    }

    return drv->execute(sql, params);
}

db_result orm_engine::query(const std::string &sql,
                            const std::vector<db_value> &params)
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        db_result res;

        res.success = false;
        res.error_message = "No database driver available.";

        return res;
    }

    return drv->query(sql, params);
}

bool orm_engine::scaffold(const table_schema &schema)
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return false;
    }

    return drv->scaffold_table(schema);
}

bool orm_engine::table_exists(const std::string &table_name)
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return false;
    }

    return drv->table_exists(table_name);
}

bool orm_engine::is_connected() const
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return false;
    }

    return drv->is_connected();
}

std::string orm_engine::driver_name() const
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return "none";
    }

    return drv->driver_name();
}

std::string singularize_word(const std::string &word)
{
    if (word.size() > 3 && word.substr(word.size() - 3) == "ies")
    {
        return word.substr(0, word.size() - 3) + "y";
    }

    if (word.size() > 2 && word.substr(word.size() - 2) == "es")
    {
        std::string stem = word.substr(0, word.size() - 2);

        if (stem.ends_with("ch") || stem.ends_with("sh") ||
            stem.ends_with("ss") || stem.ends_with("x"))
        {
            return stem;
        }
    }

    if (word.size() > 1 && word.back() == 's' && !word.ends_with("ss"))
    {
        return word.substr(0, word.size() - 1);
    }

    return word;
}

std::string table_to_class_name(const std::string &table_name)
{
    std::string result;
    std::istringstream stream(table_name);
    std::string part;
    std::vector<std::string> parts;

    while (std::getline(stream, part, '_'))
    {
        if (!part.empty())
        {
            parts.push_back(part);
        }
    }

    if (parts.empty())
    {
        return "Model";
    }

    parts.back() = singularize_word(parts.back());

    for (auto &p : parts)
    {
        if (!p.empty())
        {
            p[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(p[0])));

            for (size_t i = 1; i < p.size(); ++i)
            {
                p[i] = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(p[i])));
            }

            result += p;
        }
    }

    return result;
}

std::string table_to_model_filename(const std::string &table_name)
{
    std::string result;
    std::istringstream stream(table_name);
    std::string part;
    std::vector<std::string> parts;

    while (std::getline(stream, part, '_'))
    {
        if (!part.empty())
        {
            parts.push_back(part);
        }
    }

    if (parts.empty())
    {
        return "model";
    }

    parts.back() = singularize_word(parts.back());

    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
        {
            result += "_";
        }

        for (char c : parts[i])
        {
            result +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return result;
}

std::string class_to_table_name(const std::string &class_name)
{
    if (class_name.empty())
    {
        return "";
    }

    std::string snake;

    for (size_t i = 0; i < class_name.size(); ++i)
    {
        char ch = class_name[i];

        if (std::isupper(static_cast<unsigned char>(ch)))
        {
            if (i > 0 && class_name[i - 1] != '_')
            {
                snake += '_';
            }

            snake +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        else
        {
            snake += ch;
        }
    }

    if (snake.ends_with("y") && snake.size() > 1)
    {
        char before_y = snake[snake.size() - 2];

        if (before_y != 'a' && before_y != 'e' && before_y != 'i' &&
            before_y != 'o' && before_y != 'u')
        {
            return snake.substr(0, snake.size() - 1) + "ies";
        }
    }

    if (snake.ends_with("s") || snake.ends_with("x") || snake.ends_with("z") ||
        snake.ends_with("ch") || snake.ends_with("sh"))
    {
        if (snake.ends_with("ss"))
        {
            return snake + "es";
        }

        return snake;
    }

    return snake + "s";
}

std::vector<std::string> orm_engine::get_tables()
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return {};
    }

    auto tables = drv->get_tables();

    if (!tables.empty())
    {
        return tables;
    }

    db_result res = drv->query("SHOW TABLES;");

    if (res.success && !res.rows.empty())
    {
        for (const auto &r : res.rows)
        {
            if (r.size() > 0)
            {
                tables.push_back(r.get_string(size_t(0)));
            }
        }

        return tables;
    }

    res =
        drv->query("SELECT table_name FROM information_schema.tables WHERE "
                   "table_schema NOT IN ('information_schema', 'pg_catalog');");

    if (res.success && !res.rows.empty())
    {
        for (const auto &r : res.rows)
        {
            if (r.has("table_name"))
            {
                tables.push_back(r.get_string("table_name"));
            }
            else if (r.size() > 0)
            {
                tables.push_back(r.get_string(size_t(0)));
            }
        }

        return tables;
    }

    res = drv->query("SELECT name FROM sqlite_master WHERE type='table' AND "
                     "name NOT LIKE 'sqlite_%';");

    if (res.success && !res.rows.empty())
    {
        for (const auto &r : res.rows)
        {
            if (r.has("name"))
            {
                tables.push_back(r.get_string("name"));
            }
            else if (r.size() > 0)
            {
                tables.push_back(r.get_string(size_t(0)));
            }
        }

        return tables;
    }

    return tables;
}

std::optional<table_schema>
orm_engine::describe_table(const std::string &table_name)
{
    std::shared_ptr<db_driver> drv;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        drv = active_driver_;
    }

    if (!drv)
    {
        return std::nullopt;
    }

    auto schema_opt = drv->describe_table(table_name);

    if (schema_opt.has_value())
    {
        return schema_opt;
    }

    table_schema schema;

    schema.table_name = table_name;
    schema.if_not_exists = true;

    db_result res = drv->query("DESCRIBE " + table_name + ";");

    if (res.success && !res.rows.empty())
    {
        for (const auto &r : res.rows)
        {
            column_def col;

            col.name = r.get_string("Field", r.get_string(size_t(0)));
            col.type = r.get_string("Type", "text");

            std::string key = r.get_string("Key", "");
            std::string extra = r.get_string("Extra", "");
            std::string is_null = r.get_string("Null", "YES");

            col.primary_key = (key == "PRI");
            col.auto_increment =
                (extra.find("auto_increment") != std::string::npos);
            col.not_null = (is_null == "NO");
            col.default_value = r.get_string("Default", "");

            schema.columns.push_back(std::move(col));
        }

        return schema;
    }

    res = drv->query("PRAGMA table_info(" + table_name + ");");

    if (res.success && !res.rows.empty())
    {
        for (const auto &r : res.rows)
        {
            column_def col;

            col.name = r.get_string("name", r.get_string(size_t(1)));
            col.type = r.get_string("type", "text");
            col.not_null = (r.get_int("notnull", 0) == 1);
            col.primary_key = (r.get_int("pk", 0) >= 1);
            col.default_value = r.get_string("dflt_value", "");

            std::string t_upper = col.type;

            std::transform(t_upper.begin(), t_upper.end(), t_upper.begin(),
                           [](unsigned char ch)
                           { return static_cast<char>(std::toupper(ch)); });

            if (col.primary_key && t_upper.find("INT") != std::string::npos)
            {
                col.auto_increment = true;
            }

            schema.columns.push_back(std::move(col));
        }

        return schema;
    }

    res = drv->query("SELECT * FROM " + table_name + " WHERE 1=0;");

    if (res.success && !res.column_names.empty())
    {
        for (const auto &col_name : res.column_names)
        {
            column_def col;

            col.name = col_name;
            col.type = (col_name == "id") ? "integer" : "text";
            col.primary_key = (col_name == "id");
            col.auto_increment = (col_name == "id");

            schema.columns.push_back(std::move(col));
        }

        return schema;
    }

    return std::nullopt;
}

std::string
orm_engine::generate_model_code(const std::string &table_name,
                                const std::optional<table_schema> &schema_opt)
{
    std::string class_name = table_to_class_name(table_name);
    std::string pk = "id";
    bool has_created_at = false;
    bool has_updated_at = false;

    std::vector<column_def> columns;

    if (schema_opt.has_value())
    {
        columns = schema_opt->columns;
    }
    else
    {
        auto desc = describe_table(table_name);

        if (desc.has_value())
        {
            columns = desc->columns;
        }
    }

    for (const auto &c : columns)
    {
        if (c.primary_key)
        {
            pk = c.name;
        }

        if (c.name == "created_at")
        {
            has_created_at = true;
        }

        if (c.name == "updated_at")
        {
            has_updated_at = true;
        }
    }

    bool has_timestamps =
        columns.empty() ? true : (has_created_at && has_updated_at);

    std::ostringstream ss;

    ss << "-- "
          "════════════════════════════════════════════════════════════════════"
          "═══════\n";
    ss << "-- Model: " << class_name << "\n";
    ss << "-- Table: " << table_name << "\n";
    ss << "-- Auto-generated by Eloquent Scaffold Generator\n";
    ss << "-- "
          "════════════════════════════════════════════════════════════════════"
          "═══════\n\n";

    ss << "local Model = _G.Model or require(\"eloquent\").Model\n\n";

    if (columns.empty())
    {
        ss << "---@class " << class_name << "\n";
        ss << "---@field id number Primary Key\n";
        ss << "---@field name string\n";
        ss << "---@field created_at string\n";
        ss << "---@field updated_at string\n\n";
    }
    else
    {
        ss << "---@class " << class_name << "\n";

        for (const auto &c : columns)
        {
            std::string lua_type = "string";
            std::string t = c.type;

            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            if (t.find("int") != std::string::npos ||
                t.find("numeric") != std::string::npos ||
                t.find("float") != std::string::npos ||
                t.find("double") != std::string::npos ||
                t.find("real") != std::string::npos ||
                t.find("decimal") != std::string::npos)
            {
                lua_type = (t.find("tinyint(1)") != std::string::npos ||
                            t.find("bool") != std::string::npos)
                               ? "boolean"
                               : "number";
            }
            else if (t.find("bool") != std::string::npos)
            {
                lua_type = "boolean";
            }

            ss << "---@field " << c.name << " " << lua_type;

            if (c.primary_key)
            {
                ss << " Primary Key";
            }

            ss << "\n";
        }

        ss << "\n";
    }

    ss << "local " << class_name << " = Model.extend(\"" << table_name
       << "\", {\n";
    ss << "    primary_key = \"" << pk << "\",\n";
    ss << "    timestamps = " << (has_timestamps ? "true" : "false") << "\n";
    ss << "})\n\n";

    ss << "return " << class_name << "\n";

    return ss.str();
}

std::string orm_engine::generate_migration_code(
    const std::string &table_name,
    const std::optional<table_schema> &schema_opt)
{
    std::vector<column_def> columns;

    if (schema_opt.has_value())
    {
        columns = schema_opt->columns;
    }
    else
    {
        auto desc = describe_table(table_name);

        if (desc.has_value())
        {
            columns = desc->columns;
        }
    }

    std::ostringstream ss;

    ss << "-- "
          "════════════════════════════════════════════════════════════════════"
          "═══════\n";
    ss << "-- Migration: create_" << table_name << "_table\n";
    ss << "-- Table: " << table_name << "\n";
    ss << "-- Auto-generated by Eloquent Scaffold Generator\n";
    ss << "-- "
          "════════════════════════════════════════════════════════════════════"
          "═══════\n\n";

    ss << "local Schema = _G.Schema or require(\"eloquent\").Schema\n\n";

    ss << "return {\n";
    ss << "    up = function()\n";
    ss << "        return Schema.create(\"" << table_name
       << "\", function(table)\n";

    bool has_created_at = false;
    bool has_updated_at = false;

    for (const auto &c : columns)
    {
        if (c.name == "created_at")
        {
            has_created_at = true;
        }

        if (c.name == "updated_at")
        {
            has_updated_at = true;
        }
    }

    bool timestamps_condensed = (has_created_at && has_updated_at);

    if (columns.empty())
    {
        ss << "            table:id()\n";
        ss << "            table:string(\"name\")\n";
        ss << "            table:timestamps()\n";
    }
    else
    {
        for (const auto &c : columns)
        {
            if (timestamps_condensed &&
                (c.name == "created_at" || c.name == "updated_at"))
            {
                continue;
            }

            std::string t = c.type;

            std::transform(t.begin(), t.end(), t.begin(), [](unsigned char ch)
                           { return static_cast<char>(std::tolower(ch)); });

            ss << "            ";

            if (c.primary_key && (c.name == "id" || c.auto_increment))
            {
                if (t.find("bigint") != std::string::npos)
                {
                    ss << "table:bigIncrements(\"" << c.name << "\")";
                }
                else
                {
                    ss << "table:id(\"" << c.name << "\")";
                }
            }
            else if (t.find("varchar") != std::string::npos ||
                     t.find("char") != std::string::npos)
            {
                size_t p1 = t.find('(');
                size_t p2 = t.find(')');

                if (p1 != std::string::npos && p2 != std::string::npos &&
                    p2 > p1)
                {
                    std::string len = t.substr(p1 + 1, p2 - p1 - 1);

                    ss << "table:string(\"" << c.name << "\", " << len << ")";
                }
                else
                {
                    ss << "table:string(\"" << c.name << "\")";
                }
            }
            else if (t.find("text") != std::string::npos)
            {
                ss << "table:text(\"" << c.name << "\")";
            }
            else if (t.find("tinyint(1)") != std::string::npos ||
                     t.find("bool") != std::string::npos)
            {
                ss << "table:boolean(\"" << c.name << "\")";
            }
            else if (t.find("bigint") != std::string::npos)
            {
                ss << "table:bigInteger(\"" << c.name << "\")";
            }
            else if (t.find("int") != std::string::npos)
            {
                ss << "table:integer(\"" << c.name << "\")";
            }
            else if (t.find("decimal") != std::string::npos ||
                     t.find("numeric") != std::string::npos)
            {
                ss << "table:decimal(\"" << c.name << "\")";
            }
            else if (t.find("float") != std::string::npos ||
                     t.find("double") != std::string::npos ||
                     t.find("real") != std::string::npos)
            {
                ss << "table:real(\"" << c.name << "\")";
            }
            else if (t.find("datetime") != std::string::npos)
            {
                ss << "table:datetime(\"" << c.name << "\")";
            }
            else if (t.find("timestamp") != std::string::npos)
            {
                ss << "table:timestamp(\"" << c.name << "\")";
            }
            else if (t.find("blob") != std::string::npos ||
                     t.find("binary") != std::string::npos)
            {
                ss << "table:blob(\"" << c.name << "\")";
            }
            else
            {
                ss << "table:string(\"" << c.name << "\")";
            }

            if (!c.primary_key)
            {
                if (!c.not_null)
                {
                    ss << ":nullable()";
                }
                else
                {
                    ss << ":notNull()";
                }

                if (c.unique)
                {
                    ss << ":unique()";
                }

                if (!c.default_value.empty() && c.default_value != "NULL")
                {
                    ss << ":default(\"" << c.default_value << "\")";
                }
            }

            ss << "\n";
        }

        if (timestamps_condensed)
        {
            ss << "            table:timestamps()\n";
        }
    }

    ss << "        end)\n";
    ss << "    end,\n\n";

    ss << "    down = function()\n";
    ss << "        return Schema.dropIfExists(\"" << table_name << "\")\n";
    ss << "    end\n";
    ss << "}\n";

    return ss.str();
}

bool orm_engine::scaffold_table_files(const std::string &table_name,
                                      const std::string &output_models_dir,
                                      const std::string &output_migrations_dir)
{
    namespace fs = std::filesystem;

    std::error_code ec;

    if (!fs::exists(output_models_dir, ec))
    {
        fs::create_directories(output_models_dir, ec);
    }

    if (!fs::exists(output_migrations_dir, ec))
    {
        fs::create_directories(output_migrations_dir, ec);
    }

    auto schema = describe_table(table_name);

    std::string model_code = generate_model_code(table_name, schema);
    std::string migration_code = generate_migration_code(table_name, schema);

    std::string model_filename = table_to_model_filename(table_name) + ".lua";
    std::string model_path =
        (fs::path(output_models_dir) / model_filename).string();

    std::ofstream model_file(model_path);

    if (model_file.is_open())
    {
        model_file << model_code;
        model_file.close();
    }

    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};

    __localtime_(&tt, &tm_buf);

    std::ostringstream prefix;

    prefix << std::put_time(&tm_buf, "%Y_%m_%d_%H%M%S");

    std::string mig_filename =
        prefix.str() + "_create_" + table_name + "_table.lua";
    std::string mig_path =
        (fs::path(output_migrations_dir) / mig_filename).string();

    std::ofstream mig_file(mig_path);

    if (mig_file.is_open())
    {
        mig_file << migration_code;
        mig_file.close();
    }

    LOG_INFO("orm", "Scaffolded table '" << table_name
                                         << "' -> Model: " << model_path
                                         << ", Migration: " << mig_path);

    return true;
}

size_t orm_engine::scaffold_all_tables(const std::string &output_models_dir,
                                       const std::string &output_migrations_dir)
{
    auto tables = get_tables();
    size_t count = 0;

    for (const auto &t : tables)
    {
        if (scaffold_table_files(t, output_models_dir, output_migrations_dir))
        {
            count++;
        }
    }

    LOG_INFO("orm", "Scaffolded " << count << " table(s) from database into "
                                  << output_models_dir << " and "
                                  << output_migrations_dir);

    return count;
}

void orm_engine::bind_lua(sol::state &lua)
{
    lua.new_usertype<column_def>(
        "column_def", sol::constructors<column_def()>(), "name",
        &column_def::name, "type", &column_def::type, "primary_key",
        &column_def::primary_key, "auto_increment", &column_def::auto_increment,
        "not_null", &column_def::not_null, "unique", &column_def::unique,
        "default_value", &column_def::default_value);

    lua.new_usertype<table_schema>(
        "table_schema", sol::constructors<table_schema()>(), "table_name",
        &table_schema::table_name, "if_not_exists",
        &table_schema::if_not_exists, "add_column",
        [](table_schema &s, const column_def &c) { s.columns.push_back(c); });

    lua.new_usertype<db_row>(
        "db_row", sol::constructors<db_row()>(), "size", &db_row::size, "has",
        &db_row::has, "get_string",
        sol::overload(
            [](const db_row &r, const std::string &name) -> std::string
            { return r.get_string(name); },
            [](const db_row &r, const std::string &name,
               const std::string &def) -> std::string
            { return r.get_string(name, def); },
            [](const db_row &r, size_t index) -> std::string
            { return r.get_string(index); },
            [](const db_row &r, size_t index,
               const std::string &def) -> std::string
            { return r.get_string(index, def); }),
        "get_int",
        sol::overload(
            [](const db_row &r, const std::string &name) -> int64_t
            { return r.get_int(name); },
            [](const db_row &r, const std::string &name, int64_t def) -> int64_t
            { return r.get_int(name, def); },
            [](const db_row &r, size_t index) -> int64_t
            { return r.get_int(index); },
            [](const db_row &r, size_t index, int64_t def) -> int64_t
            { return r.get_int(index, def); }),
        "get_double", &db_row::get_double, "get_bool", &db_row::get_bool);

    lua.new_usertype<db_result>(
        "db_result", sol::constructors<db_result()>(), "success",
        &db_result::success, "error_message", &db_result::error_message,
        "affected_rows", &db_result::affected_rows, "last_insert_id",
        &db_result::last_insert_id, "row_count", &db_result::row_count, "empty",
        &db_result::empty);

    sol::table api_tbl = lua["api"].get_or_create<sol::table>();
    sol::table db_ns = api_tbl["db"].get_or_create<sol::table>();

    db_ns["column_def"] = lua["column_def"];
    db_ns["table_schema"] = lua["table_schema"];
    db_ns["db_row"] = lua["db_row"];
    db_ns["db_result"] = lua["db_result"];
    db_ns["is_connected"] = [this]() -> bool { return is_connected(); };
    db_ns["driver_name"] = [this]() -> std::string { return driver_name(); };

    sol::table db_tbl = lua.create_named_table("DB");

    lua["db"] = db_tbl;

    db_tbl["execute"] = [this, &lua](sol::variadic_args args) -> sol::table
    {
        std::string sql;
        std::vector<db_value> params;

        parse_args(args, sql, params);

        sol::table ret = lua.create_table();

        if (sql.empty())
        {
            ret["success"] = false;
            ret["affected_rows"] = 0;
            ret["last_insert_id"] = 0;
            ret["error"] = "SQL query string cannot be empty";

            return ret;
        }

        db_result res = execute(sql, params);

        ret["success"] = res.success;
        ret["affected_rows"] = res.affected_rows;
        ret["last_insert_id"] = res.last_insert_id;

        if (!res.success)
        {
            ret["error"] = res.error_message;
        }

        return ret;
    };

    db_tbl["query"] = [this, &lua](sol::variadic_args args) -> sol::table
    {
        std::string sql;
        std::vector<db_value> params;

        parse_args(args, sql, params);

        sol::table ret = lua.create_table();

        if (sql.empty())
        {
            ret["success"] = false;
            ret["affected_rows"] = 0;
            ret["error"] = "SQL query string cannot be empty";

            return ret;
        }

        db_result res = query(sql, params);

        ret["success"] = res.success;
        ret["affected_rows"] = res.affected_rows;

        if (!res.success)
        {
            ret["error"] = res.error_message;

            return ret;
        }

        for (size_t i = 0; i < res.rows.size(); ++i)
        {
            const auto &row = res.rows[i];
            sol::table row_tbl = lua.create_table();

            for (size_t c = 0; c < row.column_names.size(); ++c)
            {
                row_tbl[row.column_names[c]] =
                    db_value_to_sol(lua, row.values[c]);
            }

            ret[i + 1] = row_tbl;
        }

        return ret;
    };

    db_tbl["query_row"] = [this, &lua](sol::variadic_args args) -> sol::object
    {
        std::string sql;
        std::vector<db_value> params;

        parse_args(args, sql, params);

        if (sql.empty())
        {
            return sol::nil;
        }

        db_result res = query(sql, params);

        if (!res.success || res.rows.empty())
        {
            return sol::nil;
        }

        const auto &row = res.rows[0];
        sol::table row_tbl = lua.create_table();

        for (size_t c = 0; c < row.column_names.size(); ++c)
        {
            row_tbl[row.column_names[c]] = db_value_to_sol(lua, row.values[c]);
        }

        return row_tbl;
    };

    db_tbl["scaffold"] = [this](sol::variadic_args args) -> bool
    {
        size_t start_idx = 0;

        if (args.size() > 0)
        {
            sol::object first = args[0];

            if (first.is<sol::table>())
            {
                sol::table t = first.as<sol::table>();

                if (t["scaffold"].valid())
                {
                    start_idx = 1;
                }
            }
        }

        if (args.size() < start_idx + 2)
        {
            return false;
        }

        std::string table_name = args[start_idx].as<std::string>();
        sol::table schema_tbl = args[start_idx + 1].as<sol::table>();

        table_schema schema;

        schema.table_name = table_name;
        schema.if_not_exists = true;

        schema_tbl.for_each(
            [&schema](sol::object key, sol::object val)
            {
                if (!val.is<sol::table>())
                {
                    return;
                }

                sol::table def_tbl = val.as<sol::table>();
                column_def col;

                if (key.is<std::string>())
                {
                    col.name = key.as<std::string>();
                }
                else
                {
                    col.name = def_tbl.get_or<std::string>("name", "");
                }

                if (col.name.empty())
                {
                    return;
                }

                sol::optional<std::string> type_opt = def_tbl["type"];

                col.type = type_opt.value_or("text");

                sol::optional<bool> pri_opt = def_tbl["primary"];

                if (!pri_opt)
                {
                    pri_opt = def_tbl["primary_key"];
                }

                col.primary_key = pri_opt.value_or(false);

                sol::optional<bool> ai_opt = def_tbl["auto_increment"];

                if (!ai_opt)
                {
                    ai_opt = def_tbl["autoincrement"];
                }

                col.auto_increment = ai_opt.value_or(false);

                sol::optional<bool> nn_opt = def_tbl["not_null"];

                if (!nn_opt)
                {
                    nn_opt = def_tbl["required"];
                }

                col.not_null = nn_opt.value_or(false);

                sol::optional<bool> uq_opt = def_tbl["unique"];

                col.unique = uq_opt.value_or(false);

                sol::optional<std::string> def_opt = def_tbl["default"];

                if (!def_opt)
                {
#if defined(_WIN32) || defined(_WIN64)
                    def_opt = def_tbl["default_value"]
                        .get<sol::optional<std::string>>();
#else
                    def_opt = def_tbl["default_value"];
#endif
                }

                col.default_value = def_opt.value_or("");

                schema.columns.push_back(std::move(col));
            });

        return scaffold(schema);
    };

    db_tbl["table_exists"] = [this](const std::string &name) -> bool
    { return table_exists(name); };

    db_tbl["is_connected"] = [this]() -> bool { return is_connected(); };

    db_tbl["driver_name"] = [this]() -> std::string { return driver_name(); };

    db_tbl["drivers"] = [this, &lua]() -> sol::table
    {
        auto drivers = get_registered_drivers();
        sol::table t = lua.create_table();

        for (size_t i = 0; i < drivers.size(); ++i)
        {
            t[i + 1] = drivers[i];
        }

        return t;
    };

    db_tbl["set_driver"] = [this](const std::string &name,
                                  sol::optional<std::string> conn_str) -> bool
    { return set_active_driver(name, conn_str.value_or("")); };

    db_tbl["get_tables"] = [this](sol::this_state s) -> sol::table
    {
        sol::state_view v(s);
        sol::table t = v.create_table();
        auto tables = get_tables();

        for (size_t i = 0; i < tables.size(); ++i)
        {
            t[i + 1] = tables[i];
        }

        return t;
    };

    db_tbl["describe_table"] = [this](const std::string &name,
                                      sol::this_state s) -> sol::object
    {
        sol::state_view v(s);
        auto desc = describe_table(name);

        if (!desc.has_value())
        {
            return sol::nil;
        }

        sol::table t = v.create_table();

        t["table_name"] = desc->table_name;
        sol::table cols = v.create_table();

        for (size_t i = 0; i < desc->columns.size(); ++i)
        {
            const auto &c = desc->columns[i];
            sol::table col_tbl = v.create_table();

            col_tbl["name"] = c.name;
            col_tbl["type"] = c.type;
            col_tbl["primary"] = c.primary_key;
            col_tbl["auto_increment"] = c.auto_increment;
            col_tbl["not_null"] = c.not_null;
            col_tbl["unique"] = c.unique;
            col_tbl["default"] = c.default_value;

            cols[i + 1] = col_tbl;
            cols[c.name] = col_tbl;
        }

        t["columns"] = cols;

        return t;
    };

    db_tbl["generate_model"] =
        [this](const std::string &table_name,
               sol::optional<std::string> path_opt) -> std::string
    {
        std::string code = generate_model_code(table_name);

        if (path_opt && !path_opt.value().empty())
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path p(path_opt.value());

            if (p.has_parent_path())
            {
                fs::create_directories(p.parent_path(), ec);
            }

            std::ofstream f(path_opt.value());

            if (f.is_open())
            {
                f << code;
            }
        }

        return code;
    };

    db_tbl["generate_migration"] =
        [this](const std::string &table_name,
               sol::optional<std::string> path_opt) -> std::string
    {
        std::string code = generate_migration_code(table_name);

        if (path_opt && !path_opt.value().empty())
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path p(path_opt.value());

            if (p.has_parent_path())
            {
                fs::create_directories(p.parent_path(), ec);
            }

            std::ofstream f(path_opt.value());

            if (f.is_open())
            {
                f << code;
            }
        }

        return code;
    };

    db_tbl["scaffold_table"] =
        [this](const std::string &table_name,
               sol::optional<std::string> models_dir,
               sol::optional<std::string> migrations_dir) -> bool
    {
        return scaffold_table_files(
            table_name, models_dir.value_or("scripts/models"),
            migrations_dir.value_or("scripts/migrations"));
    };

    db_tbl["scaffold_all"] =
        [this](sol::optional<std::string> models_dir,
               sol::optional<std::string> migrations_dir) -> size_t
    {
        return scaffold_all_tables(
            models_dir.value_or("scripts/models"),
            migrations_dir.value_or("scripts/migrations"));
    };

    lua.script(R"lua(
        -- ═══════════════════════════════════════════════════════════════════
        -- Eloquent Query Builder & ORM Subsystem (Laravel Style)
        -- ═══════════════════════════════════════════════════════════════════

        local Builder = {}
        Builder.__index = Builder

        function Builder.new(table_name)
            local self = {
                _table = table_name,
                _columns = "*",
                _distinct = false,
                _wheres = {},
                _joins = {},
                _orders = {},
                _groups = {},
                _having = nil,
                _limit_val = nil,
                _offset_val = nil,
                _model = nil
            }
            return setmetatable(self, Builder)
        end

        function DB.table(table_name)
            return Builder.new(table_name)
        end

        function Builder:clone()
            local q = Builder.new(self._table)
            q._columns = self._columns
            q._distinct = self._distinct
            q._limit_val = self._limit_val
            q._offset_val = self._offset_val
            q._model = self._model
            for _, w in ipairs(self._wheres) do table.insert(q._wheres, w) end
            for _, j in ipairs(self._joins) do table.insert(q._joins, j) end
            for _, o in ipairs(self._orders) do table.insert(q._orders, o) end
            for _, g in ipairs(self._groups) do table.insert(q._groups, g) end
            q._having = self._having
            return q
        end

        -- ── Selection ─────────────────────────────────────────────────────

        function Builder:select(...)
            local args = {...}
            if #args == 1 and type(args[1]) == "table" then
                args = args[1]
            end
            if #args > 0 then
                self._columns = table.concat(args, ", ")
            end
            return self
        end

        function Builder:addSelect(...)
            local args = {...}
            if #args == 1 and type(args[1]) == "table" then
                args = args[1]
            end
            if #args > 0 then
                if self._columns == "*" then
                    self._columns = table.concat(args, ", ")
                else
                    self._columns = self._columns .. ", " .. table.concat(args, ", ")
                end
            end
            return self
        end

        function Builder:distinct()
            self._distinct = true
            return self
        end

        -- ── Where Clauses ─────────────────────────────────────────────────

        function Builder:where(col, op, val)
            if val == nil then
                val = op
                op = "="
            end
            table.insert(self._wheres, {
                type = "Basic",
                boolean = "AND",
                column = col,
                operator = string.upper(op),
                value = val
            })
            return self
        end

        function Builder:orWhere(col, op, val)
            if val == nil then
                val = op
                op = "="
            end
            table.insert(self._wheres, {
                type = "Basic",
                boolean = "OR",
                column = col,
                operator = string.upper(op),
                value = val
            })
            return self
        end

        function Builder:whereIn(col, values, boolean, not_in)
            boolean = boolean or "AND"
            table.insert(self._wheres, {
                type = "In",
                boolean = boolean,
                column = col,
                values = values,
                ["not"] = not_in or false
            })
            return self
        end

        function Builder:orWhereIn(col, values)
            return self:whereIn(col, values, "OR", false)
        end

        function Builder:whereNotIn(col, values)
            return self:whereIn(col, values, "AND", true)
        end

        function Builder:orWhereNotIn(col, values)
            return self:whereIn(col, values, "OR", true)
        end

        function Builder:whereNull(col, boolean, not_null)
            boolean = boolean or "AND"
            table.insert(self._wheres, {
                type = "Null",
                boolean = boolean,
                column = col,
                ["not"] = not_null or false
            })
            return self
        end

        function Builder:whereNotNull(col)
            return self:whereNull(col, "AND", true)
        end

        function Builder:orWhereNull(col)
            return self:whereNull(col, "OR", false)
        end

        function Builder:orWhereNotNull(col)
            return self:whereNull(col, "OR", true)
        end

        function Builder:whereBetween(col, min_val, max_val, boolean, not_between)
            boolean = boolean or "AND"
            table.insert(self._wheres, {
                type = "Between",
                boolean = boolean,
                column = col,
                min = min_val,
                max = max_val,
                ["not"] = not_between or false
            })
            return self
        end

        function Builder:whereNotBetween(col, min_val, max_val)
            return self:whereBetween(col, min_val, max_val, "AND", true)
        end

        function Builder:whereLike(col, pattern)
            return self:where(col, "LIKE", pattern)
        end

        -- ── Joins ─────────────────────────────────────────────────────────

        function Builder:join(table_name, first, op, second, join_type)
            join_type = join_type or "INNER"
            table.insert(self._joins, {
                type = join_type,
                table = table_name,
                first = first,
                operator = op,
                second = second
            })
            return self
        end

        function Builder:leftJoin(table_name, first, op, second)
            return self:join(table_name, first, op, second, "LEFT")
        end

        function Builder:rightJoin(table_name, first, op, second)
            return self:join(table_name, first, op, second, "RIGHT")
        end

        function Builder:crossJoin(table_name)
            table.insert(self._joins, {
                type = "CROSS",
                table = table_name,
                first = "",
                operator = "",
                second = ""
            })
            return self
        end

        -- ── Ordering, Grouping, Limiting ──────────────────────────────────

        function Builder:orderBy(col, dir)
            dir = string.upper(dir or "ASC")
            table.insert(self._orders, { column = col, direction = dir })
            return self
        end

        function Builder:order_by(col, dir)
            return self:orderBy(col, dir)
        end

        function Builder:orderByDesc(col)
            return self:orderBy(col, "DESC")
        end

        function Builder:latest(col)
            return self:orderBy(col or "created_at", "DESC")
        end

        function Builder:oldest(col)
            return self:orderBy(col or "created_at", "ASC")
        end

        function Builder:groupBy(...)
            local args = {...}
            if #args == 1 and type(args[1]) == "table" then
                args = args[1]
            end
            for _, g in ipairs(args) do
                table.insert(self._groups, g)
            end
            return self
        end

        function Builder:having(col, op, val)
            self._having = { column = col, operator = op, value = val }
            return self
        end

        function Builder:limit(n)
            self._limit_val = n
            return self
        end

        function Builder:take(n)
            return self:limit(n)
        end

        function Builder:offset(n)
            self._offset_val = n
            return self
        end

        function Builder:skip(n)
            return self:offset(n)
        end

        function Builder:forPage(page, per_page)
            page = math.max(1, page or 1)
            per_page = per_page or 15
            return self:offset((page - 1) * per_page):limit(per_page)
        end

        -- ── Internal SQL Compilation ──────────────────────────────────────

        function Builder:_build_wheres()
            if #self._wheres == 0 then
                return "", {}
            end
            local sql_parts = {}
            local bindings = {}
            for i, w in ipairs(self._wheres) do
                local prefix = (i == 1) and "" or (w.boolean .. " ")
                if w.type == "Basic" then
                    table.insert(sql_parts, prefix .. w.column .. " " .. w.operator .. " ?")
                    table.insert(bindings, w.value)
                elseif w.type == "In" then
                    local qmarks = {}
                    for _, val in ipairs(w.values) do
                        table.insert(qmarks, "?")
                        table.insert(bindings, val)
                    end
                    local op = w["not"] and "NOT IN" or "IN"
                    table.insert(sql_parts, prefix .. w.column .. " " .. op .. " (" .. table.concat(qmarks, ", ") .. ")")
                elseif w.type == "Null" then
                    local op = w["not"] and "IS NOT NULL" or "IS NULL"
                    table.insert(sql_parts, prefix .. w.column .. " " .. op)
                elseif w.type == "Between" then
                    local op = w["not"] and "NOT BETWEEN" or "BETWEEN"
                    table.insert(sql_parts, prefix .. w.column .. " " .. op .. " ? AND ?")
                    table.insert(bindings, w.min)
                    table.insert(bindings, w.max)
                end
            end
            return " WHERE " .. table.concat(sql_parts, " "), bindings
        end

        function Builder:_build_select()
            local dist = self._distinct and "DISTINCT " or ""
            local sql = "SELECT " .. dist .. self._columns .. " FROM " .. self._table
            for _, j in ipairs(self._joins) do
                if j.type == "CROSS" then
                    sql = sql .. " CROSS JOIN " .. j.table
                else
                    sql = sql .. " " .. j.type .. " JOIN " .. j.table .. " ON " .. j.first .. " " .. j.operator .. " " .. j.second
                end
            end
            local where_sql, bindings = self:_build_wheres()
            sql = sql .. where_sql
            if #self._groups > 0 then
                sql = sql .. " GROUP BY " .. table.concat(self._groups, ", ")
            end
            if self._having then
                sql = sql .. " HAVING " .. self._having.column .. " " .. self._having.operator .. " ?"
                table.insert(bindings, self._having.value)
            end
            if #self._orders > 0 then
                local ords = {}
                for _, o in ipairs(self._orders) do
                    table.insert(ords, o.column .. " " .. o.direction)
                end
                sql = sql .. " ORDER BY " .. table.concat(ords, ", ")
            end
            if self._limit_val then
                sql = sql .. " LIMIT " .. tostring(self._limit_val)
            end
            if self._offset_val then
                sql = sql .. " OFFSET " .. tostring(self._offset_val)
            end
            return sql, bindings
        end

        function Builder:toSql()
            local sql, _ = self:_build_select()
            return sql
        end

        function Builder:getBindings()
            local _, bindings = self:_build_select()
            return bindings
        end

        -- ── Aggregates ────────────────────────────────────────────────────

        function Builder:count(column)
            column = column or "*"
            local saved_cols = self._columns
            self._columns = "COUNT(" .. column .. ") AS aggregate"
            local sql, bindings = self:_build_select()
            self._columns = saved_cols
            local res = DB.query(sql, bindings)
            if res and #res > 0 and res[1].aggregate ~= nil then
                return tonumber(res[1].aggregate) or 0
            end
            return 0
        end

        function Builder:max(column)
            local saved_cols = self._columns
            self._columns = "MAX(" .. column .. ") AS aggregate"
            local sql, bindings = self:_build_select()
            self._columns = saved_cols
            local res = DB.query(sql, bindings)
            if res and #res > 0 then return res[1].aggregate end
            return nil
        end

        function Builder:min(column)
            local saved_cols = self._columns
            self._columns = "MIN(" .. column .. ") AS aggregate"
            local sql, bindings = self:_build_select()
            self._columns = saved_cols
            local res = DB.query(sql, bindings)
            if res and #res > 0 then return res[1].aggregate end
            return nil
        end

        function Builder:avg(column)
            local saved_cols = self._columns
            self._columns = "AVG(" .. column .. ") AS aggregate"
            local sql, bindings = self:_build_select()
            self._columns = saved_cols
            local res = DB.query(sql, bindings)
            if res and #res > 0 and res[1].aggregate ~= nil then
                return tonumber(res[1].aggregate) or 0
            end
            return 0
        end

        function Builder:sum(column)
            local saved_cols = self._columns
            self._columns = "SUM(" .. column .. ") AS aggregate"
            local sql, bindings = self:_build_select()
            self._columns = saved_cols
            local res = DB.query(sql, bindings)
            if res and #res > 0 and res[1].aggregate ~= nil then
                return tonumber(res[1].aggregate) or 0
            end
            return 0
        end

        function Builder:exists()
            local q = self:clone()
            q._columns = "1"
            q:limit(1)
            local sql, bindings = q:_build_select()
            local res = DB.query(sql, bindings)
            return (res and #res > 0)
        end

        function Builder:doesntExist()
            return not self:exists()
        end

        -- ── Retrieval ─────────────────────────────────────────────────────

        function Builder:get()
            local sql, bindings = self:_build_select()
            local rows = DB.query(sql, bindings)
            if self._model and rows then
                local models = {}
                for i, r in ipairs(rows) do
                    models[i] = self._model.new(r, true)
                end
                return models
            end
            return rows
        end

        function Builder:first()
            self:limit(1)
            local rows = self:get()
            if rows and #rows > 0 then
                return rows[1]
            end
            return nil
        end

        function Builder:firstOrFail()
            local r = self:first()
            if not r then
                error("No query results for table: " .. self._table)
            end
            return r
        end

        function Builder:find(id)
            local pk = (self._model and self._model._primaryKey) or "id"
            return self:where(pk, id):first()
        end

        function Builder:findOrFail(id)
            local r = self:find(id)
            if not r then
                error("No record found with primary key " .. tostring(id) .. " in table: " .. self._table)
            end
            return r
        end

        function Builder:value(column)
            local r = self:select(column):first()
            if r then
                return r[column]
            end
            return nil
        end

        function Builder:pluck(column, key_column)
            local cols = key_column and { column, key_column } or { column }
            self:select(cols)
            local rows = self:get()
            local results = {}
            if key_column then
                for _, r in ipairs(rows) do
                    if r[key_column] ~= nil then
                        results[r[key_column]] = r[column]
                    end
                end
            else
                for _, r in ipairs(rows) do
                    table.insert(results, r[column])
                end
            end
            return results
        end

        function Builder:paginate(per_page, page)
            per_page = per_page or 15
            page = math.max(1, page or 1)
            local count_query = self:clone()
            local total = count_query:count()
            self:forPage(page, per_page)
            local items = self:get()
            return {
                data = items,
                total = total,
                per_page = per_page,
                current_page = page,
                last_page = math.max(1, math.ceil(total / per_page))
            }
        end

        -- ── Mutations ─────────────────────────────────────────────────────

        function Builder:insert(data)
            if not data or not next(data) then
                return { success = false, error = "Cannot insert empty data" }
            end
            local cols = {}
            local qmarks = {}
            local params = {}
            for k, v in pairs(data) do
                table.insert(cols, k)
                table.insert(qmarks, "?")
                table.insert(params, v)
            end
            local sql = "INSERT INTO " .. self._table .. " (" .. table.concat(cols, ", ") .. ") VALUES (" .. table.concat(qmarks, ", ") .. ")"
            return DB.execute(sql, params)
        end

        function Builder:insertGetId(data)
            local res = self:insert(data)
            if res and res.success then
                return res.last_insert_id
            end
            return nil
        end

        function Builder:update(data)
            if not data or not next(data) then
                return { success = false, affected_rows = 0 }
            end
            local set_parts = {}
            local params = {}
            for k, v in pairs(data) do
                table.insert(set_parts, k .. " = ?")
                table.insert(params, v)
            end
            local where_sql, where_bindings = self:_build_wheres()
            for _, b in ipairs(where_bindings) do
                table.insert(params, b)
            end
            local sql = "UPDATE " .. self._table .. " SET " .. table.concat(set_parts, ", ") .. where_sql
            return DB.execute(sql, params)
        end

        function Builder:updateOrInsert(attributes, values)
            local q = self:clone()
            for k, v in pairs(attributes) do
                q:where(k, v)
            end
            if q:exists() then
                if values and next(values) then
                    return q:update(values)
                end
                return { success = true, affected_rows = 0 }
            else
                local merged = {}
                for k, v in pairs(attributes) do merged[k] = v end
                if values then
                    for k, v in pairs(values) do merged[k] = v end
                end
                return self:insert(merged)
            end
        end

        function Builder:delete()
            local where_sql, bindings = self:_build_wheres()
            local sql = "DELETE FROM " .. self._table .. where_sql
            return DB.execute(sql, bindings)
        end

        function Builder:truncate()
            local sql = "DELETE FROM " .. self._table
            return DB.execute(sql)
        end

        function Builder:increment(col, amount)
            amount = amount or 1
            local where_sql, bindings = self:_build_wheres()
            local params = { amount }
            for _, b in ipairs(bindings) do
                table.insert(params, b)
            end
            local sql = "UPDATE " .. self._table .. " SET " .. col .. " = " .. col .. " + ?" .. where_sql
            return DB.execute(sql, params)
        end

        function Builder:decrement(col, amount)
            amount = amount or 1
            local where_sql, bindings = self:_build_wheres()
            local params = { amount }
            for _, b in ipairs(bindings) do
                table.insert(params, b)
            end
            local sql = "UPDATE " .. self._table .. " SET " .. col .. " = " .. col .. " - ?" .. where_sql
            return DB.execute(sql, params)
        end

        -- ═══════════════════════════════════════════════════════════════════
        -- Eloquent Model (Active Record Pattern)
        -- ═══════════════════════════════════════════════════════════════════

        local Model = {}
        Model.__index = Model

        function Model.extend(table_name, options)
            options = options or {}
            local ModelClass = {
                _table = table_name,
                _primaryKey = options.primary_key or options.primaryKey or "id",
                _timestamps = options.timestamps or false
            }

            local ClassMeta = {
                __index = function(t, key)
                    if Model[key] then
                        return Model[key]
                    end
                    return function(self, ...)
                        local q = ModelClass.query()
                        local fn = q[key]
                        if type(fn) == "function" then
                            return fn(q, ...)
                        end
                        error("Method '" .. tostring(key) .. "' does not exist on Model or QueryBuilder for table '" .. table_name .. "'")
                    end
                end,
                __call = function(t, attrs)
                    return ModelClass.new(attrs, false)
                end
            }
            setmetatable(ModelClass, ClassMeta)

            function ModelClass.query()
                local q = Builder.new(table_name)
                q._model = ModelClass
                return q
            end

            function ModelClass.new(attributes, from_db)
                local inst = {
                    _class = ModelClass,
                    _attributes = {},
                    _original = {},
                    _exists = from_db or false
                }
                if attributes then
                    for k, v in pairs(attributes) do
                        inst._attributes[k] = v
                        if from_db then
                            inst._original[k] = v
                        end
                    end
                end

                local InstMeta = {
                    __index = function(self, key)
                        if key == "_attributes" or key == "_original" or key == "_class" or key == "_exists" then
                            return rawget(self, key)
                        end
                        local val = self._attributes[key]
                        if val ~= nil then
                            return val
                        end
                        return ModelClass[key] or Model[key]
                    end,
                    __newindex = function(self, key, val)
                        self._attributes[key] = val
                    end,
                    __tostring = function(self)
                        return self:toJson()
                    end
                }
                setmetatable(inst, InstMeta)
                return inst
            end

            function ModelClass.find(id)
                return ModelClass.query():find(id)
            end

            function ModelClass.findOrFail(id)
                return ModelClass.query():findOrFail(id)
            end

            function ModelClass.all()
                return ModelClass.query():get()
            end

            function ModelClass.create(attributes)
                local m = ModelClass.new(attributes, false)
                m:save()
                return m
            end

            function ModelClass.destroy(id)
                return ModelClass.query():where(ModelClass._primaryKey, id):delete()
            end

            return ModelClass
        end

        setmetatable(Model, {
            __call = function(t, table_name, options)
                return Model.extend(table_name, options)
            end
        })

        function Model:getKey()
            return self._attributes[self._class._primaryKey]
        end

        function Model:isDirty(key)
            if key then
                return self._attributes[key] ~= self._original[key]
            end
            for k, v in pairs(self._attributes) do
                if self._original[k] ~= v then
                    return true
                end
            end
            return false
        end

        function Model:save()
            local pk = self._class._primaryKey
            local now_str = os.date("!%Y-%m-%d %H:%M:%S")

            if self._exists and self._attributes[pk] then
                if self._class._timestamps then
                    self._attributes["updated_at"] = now_str
                end
                local update_data = {}
                for k, v in pairs(self._attributes) do
                    if k ~= pk then
                        update_data[k] = v
                    end
                end
                local res = DB.table(self._class._table):where(pk, self._attributes[pk]):update(update_data)
                for k, v in pairs(self._attributes) do
                    self._original[k] = v
                end
                return res
            else
                if self._class._timestamps then
                    self._attributes["created_at"] = self._attributes["created_at"] or now_str
                    self._attributes["updated_at"] = now_str
                end
                local res = DB.table(self._class._table):insert(self._attributes)
                if res and res.success and res.last_insert_id and res.last_insert_id > 0 then
                    self._attributes[pk] = res.last_insert_id
                    self._exists = true
                end
                for k, v in pairs(self._attributes) do
                    self._original[k] = v
                end
                return res
            end
        end

        function Model:delete()
            local pk = self._class._primaryKey
            if self._exists and self._attributes[pk] then
                local res = DB.table(self._class._table):where(pk, self._attributes[pk]):delete()
                self._exists = false
                return res
            end
            return { success = false, error = "Cannot delete non-persisted model" }
        end

        function Model:toArray()
            local t = {}
            for k, v in pairs(self._attributes) do
                t[k] = v
            end
            return t
        end

        function Model:toJson()
            local parts = {}
            for k, v in pairs(self._attributes) do
                local val_str = "null"
                if type(v) == "number" then
                    val_str = tostring(v)
                elseif type(v) == "boolean" then
                    val_str = v and "true" or "false"
                elseif type(v) == "string" then
                    val_str = string.format("%q", v)
                end
                table.insert(parts, string.format("%q", tostring(k)) .. ":" .. val_str)
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end

        function Model:fresh()
            local pk = self._class._primaryKey
            if self._exists and self._attributes[pk] then
                return self._class.find(self._attributes[pk])
            end
            return nil
        end

        -- ═══════════════════════════════════════════════════════════════════
        -- Eloquent Schema & Blueprint (Migrations & Scaffolding)
        -- ═══════════════════════════════════════════════════════════════════

        local Blueprint = {}
        Blueprint.__index = Blueprint

        function Blueprint.new(table_name)
            local bp = {
                _table = table_name,
                _columns = {}
            }
            return setmetatable(bp, Blueprint)
        end

        function Blueprint:_addColumn(name, col_type)
            local col = {
                name = name,
                type = col_type,
                primary = false,
                auto_increment = false,
                not_null = true,
                unique = false,
                default = nil
            }
            table.insert(self._columns, col)

            local modifier = {
                nullable = function(m)
                    col.not_null = false
                    return m
                end,
                notNull = function(m)
                    col.not_null = true
                    return m
                end,
                unique = function(m)
                    col.unique = true
                    return m
                end,
                default = function(m, val)
                    col.default = tostring(val)
                    return m
                end,
                primary = function(m)
                    col.primary = true
                    return m
                end,
                autoIncrement = function(m)
                    col.auto_increment = true
                    return m
                end
            }
            return modifier
        end

        function Blueprint:id(name)
            local m = self:_addColumn(name or "id", "integer")
            m:primary()
            m:autoIncrement()
            return m
        end

        function Blueprint:increments(name)
            return self:id(name)
        end

        function Blueprint:string(name, length)
            return self:_addColumn(name, "text")
        end

        function Blueprint:text(name)
            return self:_addColumn(name, "text")
        end

        function Blueprint:integer(name)
            return self:_addColumn(name, "integer")
        end

        function Blueprint:bigInteger(name)
            return self:_addColumn(name, "integer")
        end

        function Blueprint:boolean(name)
            return self:_addColumn(name, "integer")
        end

        function Blueprint:real(name)
            return self:_addColumn(name, "real")
        end

        function Blueprint:float(name)
            return self:_addColumn(name, "real")
        end

        function Blueprint:double(name)
            return self:_addColumn(name, "real")
        end

        function Blueprint:datetime(name)
            return self:_addColumn(name, "text")
        end

        function Blueprint:timestamp(name)
            return self:_addColumn(name, "text")
        end

        function Blueprint:blob(name)
            return self:_addColumn(name, "blob")
        end

        function Blueprint:timestamps()
            self:_addColumn("created_at", "text"):nullable()
            self:_addColumn("updated_at", "text"):nullable()
        end

        local Schema = {}

        function Schema.create(table_name, callback)
            local bp = Blueprint.new(table_name)
            if callback then
                callback(bp)
            end
            local schema_def = {}
            for _, col in ipairs(bp._columns) do
                schema_def[col.name] = {
                    name = col.name,
                    type = col.type,
                    primary = col.primary,
                    auto_increment = col.auto_increment,
                    not_null = col.not_null,
                    unique = col.unique,
                    default = col.default
                }
            end
            return DB.scaffold(table_name, schema_def)
        end

        function Schema.drop(table_name)
            return DB.execute("DROP TABLE " .. table_name)
        end

        function Schema.dropIfExists(table_name)
            return DB.execute("DROP TABLE IF EXISTS " .. table_name)
        end

        function Schema.hasTable(table_name)
            return DB.table_exists(table_name)
        end

        -- ═══════════════════════════════════════════════════════════════════
        -- Artisan Facade (Scaffolding & Code Generation)
        -- ═══════════════════════════════════════════════════════════════════

        local Artisan = {}

        function Artisan.tables()
            if DB.get_tables then
                return DB.get_tables()
            end

            return {}
        end

        function Artisan.describe(table_name)
            if DB.describe_table then
                return DB.describe_table(table_name)
            end

            return {}
        end

        function Artisan.make_model(table_name, target_file)
            if DB.generate_model then
                return DB.generate_model(table_name, target_file)
            end

            return ""
        end

        function Artisan.make_migration(table_name, target_file)
            if DB.generate_migration then
                return DB.generate_migration(table_name, target_file)
            end

            return ""
        end

        function Artisan.scaffold(table_name, models_dir, migrations_dir)
            if DB.scaffold_table then
                return DB.scaffold_table(table_name, models_dir, migrations_dir)
            end

            return false
        end

        function Artisan.scaffold_all(models_dir, migrations_dir)
            if DB.scaffold_all then
                return DB.scaffold_all(models_dir, migrations_dir)
            end

            return 0
        end

        Schema.tables = Artisan.tables
        Schema.describe = Artisan.describe
        Schema.make_model = Artisan.make_model
        Schema.make_migration = Artisan.make_migration
        Schema.scaffold_all = Artisan.scaffold_all

        DB.scaffold_all = Artisan.scaffold_all
        DB.tables = Artisan.tables
        DB.describe = Artisan.describe

        -- ── Global Exports ────────────────────────────────────────────────
        _G.Model = Model
        _G.Schema = Schema
        _G.Blueprint = Blueprint
        _G.Artisan = Artisan

        DB.Model = Model
        DB.Schema = Schema
        DB.Blueprint = Blueprint
        DB.Builder = Builder
        DB.Artisan = Artisan

        -- ── Package Preload ───────────────────────────────────────────────
        local eloquent_module = {
            Model = Model,
            Schema = Schema,
            Blueprint = Blueprint,
            Builder = Builder,
            Artisan = Artisan
        }

        package.preload["eloquent"] = function()
            return eloquent_module
        end
    )lua");
}

} // namespace api
