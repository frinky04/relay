return {
  name = "echo",
  help = "Exercise text arguments and clipboard errors",
  args = { "Text" },
  verbs = {
    { name = "Copy", run = function(args) return host.copy(args[1]) end },
  },
}
