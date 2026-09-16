-- Scalar calculator plugin. Parse math directly; never evaluate input as Lua code.
-- evaluate(text) -> number, nil, hasOperation | nil, { code, message, position }
-- format(value)  -> string | nil, { code, message, position }
-- Independent implementation; TinyExpr and math.js informed the grammar.

local calculator = {}
local MAX_BYTES, MAX_TOKENS, MAX_DEPTH, MAX_ARGS = 1024, 512, 64, 32
local constants = { pi = math.pi, e = math.exp(1) }

local function finite(value)
    return type(value) == "number" and value == value and math.abs(value) ~= math.huge
end

local function problem(code, message, position)
    return { code = code, message = message, position = position or 1 }
end

local error_tag = {}
local function abort(token, code, message)
    error(setmetatable(problem(code, message, token.position), error_tag), 0)
end

local function checked(value, token)
    if not finite(value) then
        abort(token, "overflow", "The result is outside the finite numeric range; use smaller values")
    end
    return value
end

local function tokenize(text)
    local tokens, i = {}, 1
    while i <= #text do
        local rest = text:sub(i)
        local space = rest:match("^%s+")
        if space then
            i = i + #space
        else
            local token = { position = i }
            local number = rest:match("^%d+%.?%d*") or rest:match("^%.%d+")
            local name = rest:match("^[a-zA-Z_][a-zA-Z_0-9]*")
            if number then
                local tail = rest:sub(#number + 1)
                if tail:match("^[eE]") then
                    local exponent = tail:match("^[eE][%+%-]?%d+")
                    if not exponent then
                        abort({ position = i + #number }, "number", "Add exponent digits, such as 1e3; separate the constant e with a space")
                    end
                    number = number .. exponent
                end
                token.kind, token.value = "number", checked(tonumber(number), token) + 0.0
                i = i + #number
            elseif name then
                token.kind, token.value = "name", name:lower()
                i = i + #name
            else
                local char = rest:sub(1, 1)
                if not char:match("^[%+%-%*/%^%%!(),]$") then
                    abort(token, "character", "Remove this character; use numbers, math operators, and supported names")
                end
                token.kind = char
                i = i + 1
            end
            tokens[#tokens + 1] = token
            if #tokens > MAX_TOKENS then
                abort(token, "limit", "Shorten the expression to at most 512 tokens")
            end
        end
    end
    tokens[#tokens + 1] = { kind = "end", position = #text + 1 }
    return tokens
end

local function power(a, b, token)
    if a == 0 and b < 0 then
        abort(token, "division_by_zero", "Zero cannot have a negative exponent; change the base or exponent")
    end
    if a < 0 and b % 1 ~= 0 then
        abort(token, "domain", "Use a nonnegative base or an integer exponent for a real result")
    end
    return checked(a ^ b, token)
end

local function factorial(value, token)
    if value < 0 or value % 1 ~= 0 then
        abort(token, "domain", "Use a nonnegative integer for factorial")
    end
    if value > 170 then abort(token, "overflow", "Use a factorial argument no larger than 170") end
    local result = 1.0
    for n = 2, value do result = result * n end
    return result
end

local function logarithm(x, base, token)
    if x <= 0 then abort(token, "domain", "Use a positive number for a logarithm") end
    if base and (base <= 0 or base == 1) then
        abort(token, "domain", "Use a positive logarithm base other than 1")
    end
    return base and math.log(x) / math.log(base) or math.log(x)
end

local functions = {}
local function define(name, minimum, maximum, fn)
    functions[name] = { minimum = minimum, maximum = maximum, fn = fn }
end

for name, fn in pairs({ abs = math.abs, floor = math.floor, ceil = math.ceil,
    sin = math.sin, cos = math.cos, tan = math.tan, atan = math.atan,
    exp = math.exp, rad = math.rad, deg = math.deg }) do
    define(name, 1, 1, function(args) return fn(args[1]) end)
end
define("sqrt", 1, 1, function(args, token)
    if args[1] < 0 then abort(token, "domain", "Use a nonnegative number for sqrt") end
    return math.sqrt(args[1])
end)
for name, fn in pairs({ asin = math.asin, acos = math.acos }) do
    define(name, 1, 1, function(args, token)
        if math.abs(args[1]) > 1 then abort(token, "domain", "Use a number between -1 and 1 for " .. name) end
        return fn(args[1])
    end)
end
define("atan2", 2, 2, function(args, token)
    if args[1] == 0 and args[2] == 0 then abort(token, "domain", "Use at least one nonzero coordinate for atan2") end
    return math.atan(args[1], args[2])
end)
define("ln", 1, 1, function(args, token) return logarithm(args[1], nil, token) end)
define("log", 1, 2, function(args, token) return logarithm(args[1], args[2], token) end)
define("log2", 1, 1, function(args, token) return logarithm(args[1], 2, token) end)
define("log10", 1, 1, function(args, token) return logarithm(args[1], 10, token) end)
define("pow", 2, 2, function(args, token) return power(args[1], args[2], token) end)
define("fac", 1, 1, function(args, token) return factorial(args[1], token) end)
define("min", 1, MAX_ARGS, function(args) return math.min(table.unpack(args)) end)
define("max", 1, MAX_ARGS, function(args) return math.max(table.unpack(args)) end)
define("mod", 2, 2, function(args, token)
    if args[2] == 0 then abort(token, "division_by_zero", "Use a nonzero divisor for mod") end
    return args[1] % args[2]
end)
define("round", 1, 2, function(args, token)
    local digits = args[2] or 0
    if digits % 1 ~= 0 or digits < -15 or digits > 15 then
        abort(token, "domain", "Use an integer from -15 to 15 for rounding digits")
    end
    local scale = 10.0 ^ digits
    local scaled = checked(math.abs(args[1]) * scale, token)
    local whole, fraction = math.modf(scaled)
    if fraction >= 0.5 then whole = whole + 1.0 end
    return (args[1] < 0 and -whole or whole) / scale
end)

-- Pratt binding powers: postfix > power > unary > products > sums.
-- Every intermediate is a float, avoiding Lua's wrapping integer arithmetic.
-- A percentage tag survives grouping/unary signs and sums of percentages.
-- Products, powers, function calls, and relative adjustments consume the tag.
local infix = { ["+"] = 10, ["-"] = 10, ["*"] = 20, ["/"] = 20, ["^"] = 40 }
local expression

local function peek(state)
    return state.tokens[state.index]
end

local function take(state)
    local token = peek(state)
    state.index = state.index + 1
    return token
end

local function expect(state, kind, message)
    if peek(state).kind ~= kind then abort(peek(state), "syntax", message) end
    return take(state)
end

local function call(state, name, fn)
    state.hasOperation = true
    expect(state, "(", "Add parentheses to call " .. name.value .. ", such as " .. name.value .. "(1)")
    local args = {}
    if peek(state).kind ~= ")" then
        while true do
            if #args >= MAX_ARGS then abort(peek(state), "limit", "Use at most 32 function arguments") end
            args[#args + 1] = expression(state, 0).value
            if peek(state).kind ~= "," then break end
            take(state)
        end
    end
    expect(state, ")", "Close the function call with ) and separate arguments with commas")
    if #args < fn.minimum or #args > fn.maximum then
        local count = fn.minimum == fn.maximum and tostring(fn.minimum)
            or string.format("%d to %d", fn.minimum, fn.maximum)
        abort(name, "arity", "Use " .. count .. " argument(s) for " .. name.value)
    end
    return { value = checked(fn.fn(args, name) + 0.0, name) }
end

local function prefix(state)
    local token = take(state)
    if token.kind == "number" then return { value = token.value } end
    if token.kind == "name" then
        if constants[token.value] then return { value = constants[token.value] } end
        local fn = functions[token.value]
        if fn then return call(state, token, fn) end
        abort(token, "name", "Unknown name '" .. token.value .. "'; use pi, e, or a supported function")
    end
    if token.kind == "+" or token.kind == "-" then
        local result = expression(state, 30)
        if token.kind == "-" then result.value = -result.value end
        return result
    end
    if token.kind == "(" then
        local result = expression(state, 0)
        expect(state, ")", "Close the grouped expression with )")
        return result
    end
    abort(token, "syntax", "Add a number, constant, function call, or grouped expression here")
end

local function binary(op, left, right, token)
    local a, b, percentage = left.value, right.value, nil
    local value
    if op == "+" or op == "-" then
        if right.percentage and not left.percentage then b = checked(a * b, token) end
        value = op == "+" and a + b or a - b
        percentage = left.percentage and right.percentage
    elseif op == "*" then value = a * b
    elseif op == "of" then
        if not left.percentage then abort(token, "percentage", "Put a percentage before of, such as 15% of 240") end
        value = a * b
    elseif op == "/" then
        if b == 0 then abort(token, "division_by_zero", "Use a nonzero divisor") end
        value = a / b
    elseif op == "^" then value = power(a, b, token) end
    return { value = checked(value, token), percentage = percentage }
end

expression = function(state, minimum)
    state.depth = state.depth + 1
    if state.depth > MAX_DEPTH then abort(peek(state), "limit", "Reduce expression nesting to at most 64 levels") end
    local left = prefix(state)
    while true do
        local token = peek(state)
        if (token.kind == "!" or token.kind == "%") and 50 > minimum then
            state.hasOperation = true
            take(state)
            if token.kind == "!" then left = { value = factorial(left.value, token) }
            else left = { value = left.value / 100.0, percentage = true } end
            if peek(state).kind == token.kind then
                abort(peek(state), "syntax", "Use this postfix operator once, or group the intermediate result explicitly")
            end
        else
            local op, binding, implicit = token.kind, infix[token.kind], false
            if token.kind == "name" and token.value == "of" then op, binding = "of", 20
            elseif token.kind == "(" or token.kind == "name" then op, binding, implicit = "*", 20, true end
            if not binding or binding <= minimum then break end
            state.hasOperation = true
            if not implicit then take(state) end
            local right = expression(state, op == "^" and binding - 1 or binding)
            left = binary(op, left, right, token)
        end
    end
    state.depth = state.depth - 1
    return left
end

function calculator.evaluate(text)
    if type(text) ~= "string" or #text > MAX_BYTES then
        return nil, problem("input", "Use a math expression of at most 1024 bytes")
    end
    local ok, result, hasOperation = pcall(function()
        local state = { tokens = tokenize(text), index = 1, depth = 0, hasOperation = false }
        local value = expression(state, 0).value
        if peek(state).kind ~= "end" then
            abort(peek(state), "syntax", "Remove extra input or add an operator; commas only separate function arguments")
        end
        return value, state.hasOperation
    end)
    if ok then return result, nil, hasOperation end
    if type(result) == "table" and getmetatable(result) == error_tag then
        setmetatable(result, nil)
        return nil, result
    end
    error(result, 0) -- Unexpected implementation errors are not user input errors.
end

function calculator.format(value)
    if not finite(value) then return nil, problem("value", "Supply a finite numeric result") end
    if value == 0 then return "0" end -- Normalize negative zero.
    local text
    if value % 1 == 0 and math.abs(value) <= 2 ^ 53 then text = string.format("%.0f", value)
    else text = string.format("%.15g", value) end
    -- Keep copyable decimal syntax even when the process locale uses a comma.
    return (text:gsub(",", "."))
end

-- Recognition and preview use the parser above; Copy uses the prepared result.
local function calculate(text)
  local value, err = calculator.evaluate(text)
  if value == nil then
    return nil, string.format("At byte %d: %s", err.position, err.message)
  end
  local result, formatError = calculator.format(value)
  if not result then return nil, formatError.message end
  return result
end

return {
  name = "calc",
  help = "Calculate an expression",
  args = { { name = "Expression", rest = true } },
  recognize = function(text)
    local value, _, hasOperation = calculator.evaluate(text)
    if value ~= nil and hasOperation then return { text } end
  end,
  verbs = {
    {
      name = "Copy",
      preview = function(args)
        local result, err = calculate(args[1])
        if not result then return nil, err end
        local expression = args[1]:gsub("%s+", " "):match("^%s*(.-)%s*$")
        return { title = result, subtitle = expression, value = result }
      end,
      run = function(_, value)
        return host.copy(value)
      end,
    },
  },
}
