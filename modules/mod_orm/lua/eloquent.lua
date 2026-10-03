-- ═══════════════════════════════════════════════════════════════════════════
-- mod_orm / eloquent.lua
-- Eloquent Query Builder & Active Record ORM for C++ Engine
-- Compatible with DB interface (Laravel Eloquent inspired)
-- ═══════════════════════════════════════════════════════════════════════════

local DB = _G.DB or {}

-- ═══════════════════════════════════════════════════════════════════════════
-- Eloquent Query Builder
-- ═══════════════════════════════════════════════════════════════════════════

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

function Builder:clone()
    local q = Builder.new(self._table)
    q._columns = self._columns
    q._distinct = self._distinct
    q._limit_val = self._limit_val
    q._offset_val = self._offset_val
    q._model = self._model
    q._having = self._having

    for _, w in ipairs(self._wheres) do
        table.insert(q._wheres, w)
    end
    for _, j in ipairs(self._joins) do
        table.insert(q._joins, j)
    end
    for _, o in ipairs(self._orders) do
        table.insert(q._orders, o)
    end
    for _, g in ipairs(self._groups) do
        table.insert(q._groups, g)
    end

    return q
end

-- ── Selecting Columns ───────────────────────────────────────────────────────

function Builder:select(...)
    local args = {...}
    if #args == 1 and type(args[1]) == "table" then
        args = args[1]
    end
    if #args > 0 then
        self._columns = table.concat(args, ", ")
    else
        self._columns = "*"
    end
    return self
end

function Builder:addSelect(...)
    local args = {...}
    if #args == 1 and type(args[1]) == "table" then
        args = args[1]
    end
    if self._columns == "*" then
        return self:select(...)
    end
    local extra = table.concat(args, ", ")
    self._columns = self._columns .. ", " .. extra
    return self
end

function Builder:distinct()
    self._distinct = true
    return self
end

-- ── Where Clauses ──────────────────────────────────────────────────────────

function Builder:where(...)
    local args = {...}
    if #args == 1 and type(args[1]) == "table" then
        for col, val in pairs(args[1]) do
            self:where(col, "=", val)
        end
        return self
    end

    local boolean = "AND"
    local col, op, val

    if #args == 2 then
        col = args[1]
        op = "="
        val = args[2]
    elseif #args >= 3 then
        col = args[1]
        op = args[2]
        val = args[3]
        if args[4] then
            boolean = string.upper(args[4])
        end
    end

    table.insert(self._wheres, {
        type = "basic",
        boolean = boolean,
        column = col,
        operator = op,
        value = val
    })
    return self
end

function Builder:orWhere(...)
    local args = {...}
    if #args == 1 and type(args[1]) == "table" then
        for col, val in pairs(args[1]) do
            self:where(col, "=", val, "OR")
        end
        return self
    end
    if #args == 2 then
        return self:where(args[1], "=", args[2], "OR")
    elseif #args >= 3 then
        return self:where(args[1], args[2], args[3], "OR")
    end
    return self
end

function Builder:whereIn(col, values, boolean, not_in)
    boolean = boolean or "AND"
    table.insert(self._wheres, {
        type = "in",
        boolean = boolean,
        column = col,
        values = values,
        not_in = not_in or false
    })
    return self
end

function Builder:orWhereIn(col, values)
    return self:whereIn(col, values, "OR", false)
end

function Builder:whereNotIn(col, values, boolean)
    return self:whereIn(col, values, boolean or "AND", true)
end

function Builder:orWhereNotIn(col, values)
    return self:whereIn(col, values, "OR", true)
end

function Builder:whereNull(col, boolean, not_null)
    boolean = boolean or "AND"
    table.insert(self._wheres, {
        type = "null",
        boolean = boolean,
        column = col,
        not_null = not_null or false
    })
    return self
end

function Builder:whereNotNull(col, boolean)
    return self:whereNull(col, boolean or "AND", true)
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
        type = "between",
        boolean = boolean,
        column = col,
        min_val = min_val,
        max_val = max_val,
        not_between = not_between or false
    })
    return self
end

function Builder:orWhereBetween(col, min_val, max_val)
    return self:whereBetween(col, min_val, max_val, "OR", false)
end

function Builder:whereNotBetween(col, min_val, max_val)
    return self:whereBetween(col, min_val, max_val, "AND", true)
end

function Builder:whereLike(col, pattern)
    return self:where(col, "LIKE", pattern)
end

-- ── Joins ──────────────────────────────────────────────────────────────────

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

-- ── Ordering, Grouping, Limiting ───────────────────────────────────────────

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

function Builder:having(sql, ...)
    self._having = { sql = sql, params = {...} }
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
    per_page = per_page or 15
    page = math.max(1, page or 1)
    return self:offset((page - 1) * per_page):limit(per_page)
end

-- ── SQL Compilation Internal Helpers ───────────────────────────────────────

function Builder:_build_wheres()
    if #self._wheres == 0 then
        return "", {}
    end
    local parts = {}
    local bindings = {}

    for i, w in ipairs(self._wheres) do
        local prefix = (i == 1) and "" or (" " .. w.boolean .. " ")
        if w.type == "basic" then
            table.insert(parts, prefix .. w.column .. " " .. w.operator .. " ?")
            table.insert(bindings, w.value)
        elseif w.type == "in" then
            local placeholders = {}
            for _, v in ipairs(w.values) do
                table.insert(placeholders, "?")
                table.insert(bindings, v)
            end
            local keyword = w.not_in and "NOT IN" or "IN"
            table.insert(parts, prefix .. w.column .. " " .. keyword .. " (" .. table.concat(placeholders, ", ") .. ")")
        elseif w.type == "null" then
            local keyword = w.not_null and "IS NOT NULL" or "IS NULL"
            table.insert(parts, prefix .. w.column .. " " .. keyword)
        elseif w.type == "between" then
            local keyword = w.not_between and "NOT BETWEEN" or "BETWEEN"
            table.insert(parts, prefix .. w.column .. " " .. keyword .. " ? AND ?")
            table.insert(bindings, w.min_val)
            table.insert(bindings, w.max_val)
        end
    end

    return " WHERE " .. table.concat(parts), bindings
end

function Builder:_build_select_sql()
    local distinct_str = self._distinct and "DISTINCT " or ""
    local sql = "SELECT " .. distinct_str .. self._columns .. " FROM " .. self._table
    local bindings = {}

    for _, j in ipairs(self._joins) do
        if j.type == "CROSS" then
            sql = sql .. " CROSS JOIN " .. j.table
        else
            sql = sql .. " " .. j.type .. " JOIN " .. j.table .. " ON " .. j.first .. " " .. j.operator .. " " .. j.second
        end
    end

    local where_sql, where_bindings = self:_build_wheres()
    sql = sql .. where_sql
    for _, b in ipairs(where_bindings) do
        table.insert(bindings, b)
    end

    if #self._groups > 0 then
        sql = sql .. " GROUP BY " .. table.concat(self._groups, ", ")
    end

    if self._having then
        sql = sql .. " HAVING " .. self._having.sql
        for _, p in ipairs(self._having.params) do
            table.insert(bindings, p)
        end
    end

    if #self._orders > 0 then
        local order_parts = {}
        for _, o in ipairs(self._orders) do
            table.insert(order_parts, o.column .. " " .. o.direction)
        end
        sql = sql .. " ORDER BY " .. table.concat(order_parts, ", ")
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
    local sql, _ = self:_build_select_sql()
    return sql
end

function Builder:getBindings()
    local _, bindings = self:_build_select_sql()
    return bindings
end

-- ── Retrieval & Execution ──────────────────────────────────────────────────

function Builder:get()
    local sql, bindings = self:_build_select_sql()
    local rows = DB.query(sql, bindings)
    if self._model then
        local instances = {}
        for _, row in ipairs(rows) do
            table.insert(instances, self._model.new(row, true))
        end
        return instances
    end
    return rows
end

function Builder:first()
    local q = self:clone()
    q:limit(1)
    local rows = q:get()
    if rows and #rows > 0 then
        return rows[1]
    end
    return nil
end

function Builder:firstOrFail()
    local r = self:first()
    if not r then
        error("ModelNotFoundException: No query results for model in table " .. self._table)
    end
    return r
end

function Builder:find(id)
    local pk = (self._model and self._model._primaryKey) or "id"
    return self:where(pk, id):first()
end

function Builder:findOrFail(id)
    local pk = (self._model and self._model._primaryKey) or "id"
    return self:where(pk, id):firstOrFail()
end

function Builder:value(col)
    local q = self:clone()
    q:select(col):limit(1)
    local row = q:first()
    if row then
        if type(row) == "table" and row[col] ~= nil then
            return row[col]
        end
    end
    return nil
end

function Builder:pluck(col)
    local q = self:clone()
    q:select(col)
    local rows = q:get()
    local res = {}
    for _, row in ipairs(rows) do
        table.insert(res, row[col])
    end
    return res
end

-- ── Aggregates ─────────────────────────────────────────────────────────────

function Builder:count(col)
    col = col or "*"
    local q = self:clone()
    q._columns = "COUNT(" .. col .. ") AS agg_count"
    local sql, bindings = q:_build_select_sql()
    local rows = DB.query(sql, bindings)
    if rows and #rows > 0 then
        return tonumber(rows[1].agg_count) or 0
    end
    return 0
end

function Builder:max(col)
    local q = self:clone()
    q._columns = "MAX(" .. col .. ") AS agg_val"
    local sql, bindings = q:_build_select_sql()
    local rows = DB.query(sql, bindings)
    return (rows and #rows > 0) and rows[1].agg_val or nil
end

function Builder:min(col)
    local q = self:clone()
    q._columns = "MIN(" .. col .. ") AS agg_val"
    local sql, bindings = q:_build_select_sql()
    local rows = DB.query(sql, bindings)
    return (rows and #rows > 0) and rows[1].agg_val or nil
end

function Builder:avg(col)
    local q = self:clone()
    q._columns = "AVG(" .. col .. ") AS agg_val"
    local sql, bindings = q:_build_select_sql()
    local rows = DB.query(sql, bindings)
    return (rows and #rows > 0) and (tonumber(rows[1].agg_val) or 0) or 0
end

function Builder:sum(col)
    local q = self:clone()
    q._columns = "SUM(" .. col .. ") AS agg_val"
    local sql, bindings = q:_build_select_sql()
    local rows = DB.query(sql, bindings)
    return (rows and #rows > 0) and (tonumber(rows[1].agg_val) or 0) or 0
end

function Builder:exists()
    return self:count() > 0
end

function Builder:doesntExist()
    return not self:exists()
end

function Builder:paginate(page, per_page)
    per_page = per_page or 15
    page = math.max(1, page or 1)
    local total = self:count()
    local items = self:forPage(page, per_page):get()
    return {
        current_page = page,
        per_page = per_page,
        total = total,
        last_page = math.max(1, math.ceil(total / per_page)),
        data = items
    }
end

-- ── Insert, Update, Delete ─────────────────────────────────────────────────

function Builder:insert(data)
    if not data or not next(data) then
        return { success = false, affected_rows = 0 }
    end
    local cols = {}
    local placeholders = {}
    local params = {}
    for k, v in pairs(data) do
        table.insert(cols, k)
        table.insert(placeholders, "?")
        table.insert(params, v)
    end
    local sql = "INSERT INTO " .. self._table .. " (" .. table.concat(cols, ", ") .. ") VALUES (" .. table.concat(placeholders, ", ") .. ")"
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

-- ═══════════════════════════════════════════════════════════════════════════
-- Eloquent Model (Active Record Pattern)
-- ═══════════════════════════════════════════════════════════════════════════

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

    function ModelClass.new(attributes, exists)
        local inst = {
            _attributes = {},
            _original = {},
            _exists = exists or false
        }
        if attributes then
            for k, v in pairs(attributes) do
                inst._attributes[k] = v
                if exists then
                    inst._original[k] = v
                end
            end
        end

        local InstMeta = {
            __index = function(self, key)
                if key == "_attributes" or key == "_original" or key == "_exists" then
                    return rawget(self, key)
                end
                if ModelClass[key] then
                    return ModelClass[key]
                end
                if inst._attributes[key] ~= nil then
                    return inst._attributes[key]
                end
                return nil
            end,
            __newindex = function(self, key, val)
                if key == "_attributes" or key == "_original" or key == "_exists" then
                    rawset(self, key, val)
                    return
                end
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

    function ModelClass:getKey()
        return self._attributes[ModelClass._primaryKey]
    end

    function ModelClass:isDirty(key)
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

    function ModelClass:getDirty()
        local dirty = {}
        for k, v in pairs(self._attributes) do
            if self._original[k] ~= v then
                dirty[k] = v
            end
        end
        return dirty
    end

    function ModelClass:save()
        if ModelClass._timestamps then
            local now = os.date("!%Y-%m-%d %H:%M:%S")
            self._attributes["updated_at"] = now
            if not self._exists then
                self._attributes["created_at"] = now
            end
        end

        if self._exists then
            local dirty = self:getDirty()
            if not next(dirty) then
                return true
            end
            local pk = ModelClass._primaryKey
            local res = ModelClass.query():where(pk, self:getKey()):update(dirty)
            if res.success then
                for k, v in pairs(dirty) do
                    self._original[k] = v
                end
                return true
            end
            return false
        else
            local res = ModelClass.query():insert(self._attributes)
            if res.success then
                self._exists = true
                local pk = ModelClass._primaryKey
                if res.last_insert_id and res.last_insert_id > 0 then
                    self._attributes[pk] = res.last_insert_id
                end
                for k, v in pairs(self._attributes) do
                    self._original[k] = v
                end
                return true
            end
            return false
        end
    end

    function ModelClass:delete()
        if not self._exists then
            return false
        end
        local pk = ModelClass._primaryKey
        local res = ModelClass.query():where(pk, self:getKey()):delete()
        if res.success then
            self._exists = false
            return true
        end
        return false
    end

    function ModelClass:toArray()
        local t = {}
        for k, v in pairs(self._attributes) do
            t[k] = v
        end
        return t
    end

    function ModelClass:toJson()
        local parts = {}
        for k, v in pairs(self._attributes) do
            local val_str
            if type(v) == "number" or type(v) == "boolean" then
                val_str = tostring(v)
            elseif type(v) == "nil" then
                val_str = "null"
            else
                val_str = string.format("%q", tostring(v))
            end
            table.insert(parts, string.format("%q:%s", k, val_str))
        end
        return "{" .. table.concat(parts, ",") .. "}"
    end

    function ModelClass:fresh()
        local pk = self:getKey()
        if not pk then return nil end
        return ModelClass.find(pk)
    end

    return ModelClass
end

-- ═══════════════════════════════════════════════════════════════════════════
-- Schema & Blueprint Migration Engine (Laravel Style)
-- ═══════════════════════════════════════════════════════════════════════════

local Blueprint = {}
Blueprint.__index = Blueprint

function Blueprint.new(table_name)
    local self = {
        _table = table_name,
        _columns = {}
    }
    return setmetatable(self, Blueprint)
end

function Blueprint:_addColumn(name, type_str)
    local col = {
        name = name,
        type = type_str,
        primary = false,
        auto_increment = false,
        not_null = true,
        unique = false,
        default = ""
    }

    local col_wrapper = {}
    function col_wrapper:nullable()
        col.not_null = false
        return col_wrapper
    end
    function col_wrapper:notNull()
        col.not_null = true
        return col_wrapper
    end
    function col_wrapper:unique()
        col.unique = true
        return col_wrapper
    end
    function col_wrapper:default(val)
        col.default = tostring(val)
        return col_wrapper
    end
    function col_wrapper:primary()
        col.primary = true
        return col_wrapper
    end
    function col_wrapper:autoIncrement()
        col.auto_increment = true
        return col_wrapper
    end

    table.insert(self._columns, col)
    return col_wrapper
end

function Blueprint:id(name)
    name = name or "id"
    local col = self:_addColumn(name, "integer")
    col:primary():autoIncrement()
    return col
end

function Blueprint:increments(name)
    return self:id(name)
end

function Blueprint:bigIncrements(name)
    name = name or "id"
    local col = self:_addColumn(name, "bigint")
    col:primary():autoIncrement()
    return col
end

function Blueprint:string(name, length)
    return self:_addColumn(name, length and ("varchar(" .. length .. ")") or "varchar(255)")
end

function Blueprint:text(name)
    return self:_addColumn(name, "text")
end

function Blueprint:integer(name)
    return self:_addColumn(name, "integer")
end

function Blueprint:bigInteger(name)
    return self:_addColumn(name, "bigint")
end

function Blueprint:boolean(name)
    return self:_addColumn(name, "boolean")
end

function Blueprint:real(name)
    return self:_addColumn(name, "real")
end

function Blueprint:decimal(name, precision, scale)
    precision = precision or 8
    scale = scale or 2
    return self:_addColumn(name, "decimal(" .. precision .. "," .. scale .. ")")
end

function Blueprint:float(name)
    return self:_addColumn(name, "float")
end

function Blueprint:datetime(name)
    return self:_addColumn(name, "datetime")
end

function Blueprint:timestamp(name)
    return self:_addColumn(name, "timestamp")
end

function Blueprint:timestamps()
    self:datetime("created_at"):nullable()
    self:datetime("updated_at"):nullable()
end

function Blueprint:blob(name)
    return self:_addColumn(name, "blob")
end

-- ── Schema Facade ──────────────────────────────────────────────────────────

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

-- ═══════════════════════════════════════════════════════════════════════════
-- Artisan Facade (Scaffolding & Code Generation)
-- ═══════════════════════════════════════════════════════════════════════════

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

-- ═══════════════════════════════════════════════════════════════════════════
-- Exports & Registrations
-- ═══════════════════════════════════════════════════════════════════════════

function DB.table(table_name)
    return Builder.new(table_name)
end

_G.Model = Model
_G.Schema = Schema
_G.Blueprint = Blueprint
_G.Artisan = Artisan

DB.Model = Model
DB.Schema = Schema
DB.Blueprint = Blueprint
DB.Builder = Builder
DB.Artisan = Artisan

local eloquent_module = {
    Builder = Builder,
    Model = Model,
    Schema = Schema,
    Blueprint = Blueprint,
    Artisan = Artisan
}

package.preload["eloquent"] = function()
    return eloquent_module
end

return eloquent_module
