local engines = {
  Google = "https://www.google.com/search?q=",
  DuckDuckGo = "https://duckduckgo.com/?q=",
}

local function encode(text)
  -- Encode UTF-8 bytes, preserving only URI unreserved characters.
  return (text:gsub("[^A-Za-z0-9%-._~]", function(byte)
    return string.format("%%%02X", string.byte(byte))
  end))
end

return {
  name = "web",
  help = "Search the web in your browser",
  args = {
    "Query",
    { name = "Engine", choices = { "Google", "DuckDuckGo" }, default = "Google" },
  },
  verbs = {
    {
      name = "Search",
      run = function(args)
        if not args[1]:find("%S") then
          return "Enter search terms and try again"
        end
        return host.open_url(engines[args[2]] .. encode(args[1]))
      end,
    },
  },
}
