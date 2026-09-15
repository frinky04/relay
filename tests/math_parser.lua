-- lua path/to/test_math_parser.lua
local directory = arg[0]:match("^(.*[/\\])") or "./"
local plugin = dofile(directory .. "../plugins/calc.lua")
-- Inspect private helpers in tests without adding fields to the plugin API.
local function upvalue(fn, wanted)
    for i = 1, math.huge do
        local name, value = debug.getupvalue(fn, i)
        assert(name, "Missing private helper: " .. wanted)
        if name == wanted then return value end
    end
end
local calculator = upvalue(upvalue(plugin.preview, "calculate"), "calculator")
local passed, failed = 0, 0

local function eq(actual, expected)
    assert(actual == expected, string.format("expected %s, got %s", tostring(expected), tostring(actual)))
end

local function close(actual, expected)
    assert(type(actual) == "number")
    local tolerance = expected == 0 and 1e-14 or math.abs(expected) * 1e-12
    assert(math.abs(actual - expected) <= tolerance,
        string.format("expected %.17g, got %.17g", expected, actual))
end

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1
    else failed = failed + 1; io.stderr:write(name .. ": " .. tostring(err) .. "\n") end
end

local function evaluate(text)
    local result, err = calculator.evaluate(text)
    assert(result ~= nil, err and string.format("%s at %d: %s", err.code, err.position, err.message))
    eq(err, nil)
    return result
end

local function reject(text, code, position)
    local result, err = calculator.evaluate(text)
    eq(result, nil)
    assert(type(err) == "table" and type(err.code) == "string" and type(err.message) == "string")
    assert(type(err.position) == "number" and err.position >= 1)
    if type(text) == "string" then assert(err.position <= #text + 1) end
    if code then eq(err.code, code) end
    if position then eq(err.position, position) end
    assert(not err.message:match("%.$"), "trailing period in error copy")
    eq(getmetatable(err), nil)
end

local cases = {
    { "0", 0 }, { "42", 42 }, { ".5", 0.5 }, { "1.", 1 },
    { "1.5e3 / 4", 375 }, { ".5E+2", 50 }, { "2e-3", 0.002 },
    { "1e-308", 1e-308 }, { "0012 + 03", 15 },
    { "2+3*4", 14 }, { "(120 + 35)/5", 31 }, { "(2+3)*4", 20 },
    { "12/3/2", 2 }, { "10-3-2", 5 }, { "8/2*4", 16 },
    { "2^10", 1024 }, { "2^3^2", 512 }, { "(2^3)^2", 64 },
    { "-2^2", -4 }, { "(-2)^2", 4 }, { "2^-3", 0.125 },
    { "2^-2^2", 0.0625 }, { "-2^-2", -0.25 }, { "(-2)^3", -8 },
    { "(-2)^-3", -0.125 }, { "0^0", 1 }, { "0^2", 0 },
    { "--2", 2 }, { "+-2", -2 }, { "2*-3", -6 }, { "2--3", 5 },
    { "2pi", 2 * math.pi }, { "2 e", 2 * math.exp(1) },
    { "3(4+5)", 27 }, { "(1+2)(3+4)", 21 }, { "pi(2)", 2 * math.pi },
    { "2sin(pi/2)", 2 }, { "sqrt(4)(2+3)", 10 },
    { "8/2(2+2)", 16 }, { "8/(2(2+2))", 1 }, { "2pi^2", 2 * math.pi ^ 2 },
    { "0!", 1 }, { "1!", 1 }, { "5!", 120 }, { "fac(5)", 120 },
    { "2^3!", 64 }, { "3!^2", 36 }, { "-3!", -6 }, { "(3!)!", 720 },
    { "50%", 0.5 }, { "15% of 240", 36 }, { "240-15%", 204 },
    { "200+10%", 220 }, { "200+(10%)", 220 }, { "200+(5+5)%", 220 },
    { "100+10%+10%", 121 }, { "100-120%", -20 }, { "100+-10%", 90 },
    { "-200+10%", -220 }, { "200*10%", 20 }, { "200/10%", 2000 },
    { "200+10%*2", 200.2 }, { "200+2*10%", 200.2 },
    { "10%+5%", 0.15 }, { "10%-5%", 0.05 }, { "100+(10%+5%)", 115 },
    { "(10%+5%) of 200", 30 }, { "15% of (200+40)", 36 },
    { "15% of 200+40", 70 }, { "-15% of 200", -30 },
    { "100+abs(10%)", 100.1 }, { "5!%", 1.2 }, { "50%^2", 0.25 },
    { "-50%^2", -0.25 }, { "(50%)%", 0.005 }, { "0% of 240", 0 },
    { "sqrt(144)", 12 }, { "sqrt(0)", 0 }, { "abs(-3)", 3 },
    { "floor(-1.2)", -2 }, { "ceil(-1.2)", -1 },
    { "min(1,234)", 1 }, { "max(1,234)", 234 }, { "min(5)", 5 },
    { "min(3,1+1,sqrt(16))", 2 }, { "max(-10,-2,-5)", -2 },
    { "sin(pi/2)", 1 }, { "cos(pi)", -1 }, { "tan(0)", 0 },
    { "asin(1)", math.pi/2 }, { "acos(-1)", math.pi }, { "atan(1)", math.pi/4 },
    { "atan2(1,-1)", 3*math.pi/4 }, { "sin(rad(90))", 1 }, { "deg(pi)", 180 },
    { "ln(e)", 1 }, { "log(e)", 1 }, { "log(8,2)", 3 },
    { "log2(32)", 5 }, { "log10(1000)", 3 }, { "exp(0)", 1 }, { "pow(2,10)", 1024 },
    { "mod(5,3)", 2 }, { "mod(-5,3)", 1 }, { "mod(5,-3)", -1 },
    { "mod(-5,-3)", -2 }, { "mod(5.5,2)", 1.5 },
    { "round(12.345,2)", 12.35 }, { "round(-12.345,2)", -12.35 },
    { "round(2.5)", 3 }, { "round(-2.5)", -3 }, { "round(125,-1)", 130 },
    { "round(-125,-1)", -130 }, { "round(0.49999999999999994)", 0 },
    { "  SQRT ( 144 ) + PI - pi  ", 12 }, { "2\n+\t3", 5 },
}
for _, case in ipairs(cases) do
    test(case[1], function() close(evaluate(case[1]), case[2]) end)
end

local invalid = {
    { "", "syntax", 1 }, { "2+", "syntax", 3 }, { "()", "syntax", 2 },
    { "(1+2", "syntax", 5 }, { "1,234", "syntax", 2 },
    { "2 @ 3", "character", 3 }, { "math.sqrt(4)", "character", 5 },
    { "1e", "number", 2 }, { "1e+", "number", 2 }, { "1e309", "overflow", 1 },
    { "1/0", "division_by_zero", 2 }, { "1e308*1e308", "overflow", 6 },
    { "sqrt(-1)", "domain", 1 }, { "foo(3)", "name", 1 }, { "sin()", "arity", 1 },
    { "100 of 20", "percentage", 5 }, { "15% of", "syntax", 7 },
    { "sqrt 4", "syntax", 6 }, { "sin(pi,2)", "arity", 1 },
    { "1/(-0)", "division_by_zero" }, { "mod(1,0)", "division_by_zero" },
    { "0^-1", "division_by_zero" }, { "pow(0,-1)", "division_by_zero" },
    { "(-1)^.5", "domain" }, { "pow(-1,.5)", "domain" },
    { "(-1)!", "domain" }, { "3.5!", "domain" }, { "fac(-1)", "domain" },
    { "171!", "overflow" }, { "fac(1e308)", "overflow" },
    { "log(0)", "domain" }, { "ln(-1)", "domain" }, { "log(2,1)", "domain" },
    { "log(2,0)", "domain" }, { "log(2,-2)", "domain" },
    { "asin(2)", "domain" }, { "acos(-2)", "domain" }, { "atan2(0,0)", "domain" },
    { "round(1,1.5)", "domain" }, { "round(1,16)", "domain" }, { "round(1,-16)", "domain" },
    { "exp(10000)", "overflow" }, { "1e308+1e308", "overflow" },
    { "round(1e308,15)", "overflow" }, { "1e308^2", "overflow" },
    { "1e308 + 1e308%", "overflow" }, { "1/1e-320", "overflow" },
    { "max()", "arity" }, { "ln(1,2)", "arity" }, { "round(1,2,3)", "arity" },
    { "5!!", "syntax" }, { "5%%", "syntax" },
    { "1 2", "syntax" }, { "(1+2)3", "syntax" }, { "2^", "syntax" },
    { "2**3", "syntax" }, { "2//3", "syntax" }, { "2 % 3", "syntax" },
    { "2+3 garbage", "name" }, { "pi2", "name" }, { "0xff", "name" },
    { "x=3", "character" }, { "1;2", "character" }, { "[1,2]", "character" },
    { "random()", "name" }, { "load(1)", "name" }, { "sin", "syntax" },
    { "1..2", "syntax" }, { "min(1,)", "syntax" }, { "min(,1)", "syntax" },
    { "max(1 2)", "syntax" }, { "2(3+4", "syntax" }, { "1)", "syntax" },
    { "1e2e3", "name" }, { "15 of 100%", "percentage" },
}
for _, case in ipairs(invalid) do
    test("reject " .. case[1], function() reject(case[1], case[2], case[3]) end)
end

test("input and resource limits", function()
    reject(nil, "input"); reject(42, "input"); reject({}, "input")
    reject(string.rep("1", 1025), "input")
    reject(string.rep("(", 65) .. "1" .. string.rep(")", 65), "limit")
    reject(string.rep("-", 65) .. "1", "limit")
    reject(string.rep("2^", 65) .. "1", "limit")
    reject(string.rep("abs(", 65) .. "1" .. string.rep(")", 65), "limit")
    reject(string.rep("1+", 256) .. "1", "limit")
    local args = {}
    for i = 1, 32 do args[i] = tostring(i) end
    eq(evaluate("max(" .. table.concat(args, ",") .. ")"), 32)
    args[33] = "33"
    reject("max(" .. table.concat(args, ",") .. ")", "limit")
    eq(evaluate(string.rep("1+", 250) .. "1"), 251)
end)

test("floating point avoids integer wraparound", function()
    eq(evaluate("9223372036854775807+1"), 9223372036854775808.0)
    eq(evaluate("4294967296*4294967296"), 18446744073709551616.0)
    eq(evaluate("9007199254740992+2"), 9007199254740994.0)
    assert(evaluate("170!") < math.huge)
end)

test("formatting", function()
    eq(calculator.format(evaluate("0.1+0.2")), "0.3")
    eq(calculator.format(evaluate("-0")), "0")
    eq(calculator.format(evaluate("1/3")), "0.333333333333333")
    eq(calculator.format(evaluate("2^53")), "9007199254740992")
    eq(calculator.format(evaluate("-(2^53)")), "-9007199254740992")
    eq(calculator.format(evaluate("5!")), "120")
    eq(calculator.format(evaluate("1e20")), "1e+20")
    for _, value in ipairs({ "1", false, 0/0, math.huge, -math.huge }) do
        local result, err = calculator.format(value)
        eq(result, nil); eq(err.code, "value")
    end
end)

test("independent calls have no shared expression state", function()
    local first = evaluate("15% of 240")
    reject("1+")
    evaluate("2+3")
    eq(first, 36)
    eq(evaluate("1+10%"), 1.1)
    eq(evaluate("1+10"), 11)
end)

-- This generator never evaluates source code. It computes each expected value
-- while building its expression, independently of the parser under test.
local seed = 781
local function random(n)
    seed = seed * 48271 % 2147483647
    return seed % n + 1
end

test("generated arithmetic expressions", function()
    local function generate(depth)
        if depth == 0 then
            local n = random(19) - 10
            return "(" .. n .. ")", n
        end
        local left, a = generate(depth - 1)
        local right, b = generate(depth - 1)
        local op = random(3)
        if op == 1 then return "(" .. left .. "+" .. right .. ")", a + b end
        if op == 2 then return "(" .. left .. "-" .. right .. ")", a - b end
        return "(" .. left .. "*" .. right .. ")", a * b
    end
    for _ = 1, 500 do
        local text, expected = generate(3)
        eq(evaluate(text), expected)
    end
end)

test("malformed input never raises an implementation error", function()
    local parts = { "0", ".5", "1e", "-", "+", "*", "/", "^", "%", "!", "(", ")", ",", "pi", "e", "of", "sqrt", "min", "x" }
    for _ = 1, 5000 do
        local words = {}
        for _ = 1, random(12) do words[#words + 1] = parts[random(#parts)] end
        local text = table.concat(words, " ")
        local ok, result, err = pcall(calculator.evaluate, text)
        assert(ok, text .. ": " .. tostring(result))
        if result == nil then
            assert(err and err.code and err.message, text)
            assert(err.position >= 1 and err.position <= #text + 1, text)
        else
            assert(result == result and math.abs(result) < math.huge, text)
        end
    end
end)

print(string.format("%d passed, %d failed (%s)", passed, failed, _VERSION))
assert(failed == 0, "Math parser checks failed")
