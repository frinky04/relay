local function encode(text)
  -- Encode UTF-8 bytes, preserving only URI unreserved characters.
  return (text:gsub("[^A-Za-z0-9%-._~]", function(byte)
    return string.format("%%%02X", string.byte(byte))
  end))
end

local function search(engine, url)
  return {
    name = "Search " .. engine,
    run = function(args)
      if not args[1]:find("%S") then
        return "Enter search terms and try again"
      end
      return host.open_url(url .. encode(args[1]))
    end,
  }
end

return {
  name = "web",
  help = "Search the web in your browser",
  args = {
    { name = "Query", rest = true },
  },
  verbs = {
    search("Google", "https://www.google.com/search?q="),
    search("DuckDuckGo", "https://duckduckgo.com/?q="),
  },
}
