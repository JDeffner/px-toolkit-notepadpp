// Hover markdown reduced to what a Scintilla calltip can show.
//
// A calltip is plain text in one font. The server's hover cards are markdown,
// so the marks have to go or the user reads them as content.
#pragma once

#include <string>

namespace px {

std::string markdownToPlain(const std::string& markdown);

}  // namespace px
