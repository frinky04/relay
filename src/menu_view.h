#pragma once
#include <cstddef>
#include <functional>
#include <string>

// Presentation data only. The UI attaches short activation callbacks that
// dispatch to the worker; plugin callbacks never cross into these rows.

// A recognized run of input text. The UI paints each kind differently over
// the editable text: the noun in the accent, arguments underlined, the token
// being filled underlined in the accent, verbs heavier, errors in danger.
struct TextSpan {
    size_t begin, end;
    enum Kind { Noun, Argument, Partial, Verb, Error } kind = Argument;
};

// A ghost after the caret: an unfilled argument (with its default, quoted as
// it would be typed), the default verb, or a message such as "Close the quote".
struct Slot {
    enum Kind { Argument, Verb, Message } kind;
    std::string label, value;
};

struct MenuRow {
    std::string title;
    std::string subtitle;
    std::string kind;                   // App, Command, Verb, Result, Error, Notice or a command noun; selects the gutter glyph
    std::string context;                // right-aligned: the command noun for rows found outside their command
    std::string iconKey;
    std::string completion;             // input after the offered edit; empty for action-only rows
    std::string actionLabel;            // footer text for Enter
    bool danger = false;
    bool updateCheck = false;           // preserve input and display native update status here
    // UI event handler; stayOpen records Shift+Enter. Only short UI operations
    // belong here. Command execution dispatches off the UI thread.
    std::function<void(bool stayOpen)> activate;
};
