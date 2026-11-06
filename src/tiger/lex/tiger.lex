%filenames = "scanner"

 /* Keep the upstream generator interface. */
digit [0-9]
letter [a-zA-Z]
%x COMMENT STR IGNORE

%%

"array" {adjust(); return Parser::ARRAY;}
"if" {adjust(); return Parser::IF;}
"then" {adjust(); return Parser::THEN;}
"else" {adjust(); return Parser::ELSE;}
"while" {adjust(); return Parser::WHILE;}
"for" {adjust(); return Parser::FOR;}
"to" {adjust(); return Parser::TO;}
"do" {adjust(); return Parser::DO;}
"let" {adjust(); return Parser::LET;}
"in" {adjust(); return Parser::IN;}
"end" {adjust(); return Parser::END;}
"of" {adjust(); return Parser::OF;}
"break" {adjust(); return Parser::BREAK;}
"nil" {adjust(); return Parser::NIL;}
"function" {adjust(); return Parser::FUNCTION;}
"var" {adjust(); return Parser::VAR;}
"type" {adjust(); return Parser::TYPE;}

"," {adjust(); return Parser::COMMA;}
":" {adjust(); return Parser::COLON;}
";" {adjust(); return Parser::SEMICOLON;}
"(" {adjust(); return Parser::LPAREN;}
")" {adjust(); return Parser::RPAREN;}
"[" {adjust(); return Parser::LBRACK;}
"]" {adjust(); return Parser::RBRACK;}
"{" {adjust(); return Parser::LBRACE;}
"}" {adjust(); return Parser::RBRACE;}
"." {adjust(); return Parser::DOT;}
"+" {adjust(); return Parser::PLUS;}
"-" {adjust(); return Parser::MINUS;}
"*" {adjust(); return Parser::TIMES;}
"/" {adjust(); return Parser::DIVIDE;}
"=" {adjust(); return Parser::EQ;}
"<>" {adjust(); return Parser::NEQ;}
"<" {adjust(); return Parser::LT;}
"<=" {adjust(); return Parser::LE;}
">" {adjust(); return Parser::GT;}
">=" {adjust(); return Parser::GE;}
"&" {adjust(); return Parser::AND;}
"|" {adjust(); return Parser::OR;}
":=" {adjust(); return Parser::ASSIGN;}

{letter}({letter}|{digit}|_)* {adjust(); return Parser::ID;}
{digit}+ {adjust(); return Parser::INT;}

 /* Comments nest; delimiters inside strings are ordinary string bytes. */
"/*" {adjust(); comment_pos_ = GetTokPos(); comment_level_ = 1; begin(StartCondition_::COMMENT);}
<COMMENT>"/*" {adjust(); ++comment_level_;}
<COMMENT>"*/" {adjust(); if (--comment_level_ == 0) begin(StartCondition_::INITIAL);}
<COMMENT>\n {adjust(); errormsg_->Newline();}
<COMMENT>[^*/\n]+ {adjust();}
<COMMENT>[*/] {adjust();}
<COMMENT><<EOF>> {errormsg_->Error(comment_pos_, "unterminated comment"); begin(StartCondition_::INITIAL); return 0;}

 /* Only the opening quote determines the STRING token's source position. */
\" {adjust(); string_pos_ = GetTokPos(); string_buf_.clear(); begin(StartCondition_::STR);}
<STR>\" {adjust(); errormsg_->tok_pos_ = string_pos_; setMatched(string_buf_); begin(StartCondition_::INITIAL); return Parser::STRING;}
<STR>\\n {adjust(); string_buf_ += '\n';}
<STR>\\t {adjust(); string_buf_ += '\t';}
<STR>\\\" {adjust(); string_buf_ += '"';}
<STR>\\\\ {adjust(); string_buf_ += '\\';}
<STR>\\{digit}{3} {
  adjust();
  int value = std::stoi(matched().substr(1));
  if (value > 255) errormsg_->Error(GetTokPos(), "decimal escape outside byte range");
  else string_buf_ += static_cast<char>(value);
}
<STR>\\\^[@-_] {adjust(); string_buf_ += static_cast<char>(matched()[2] - '@');}
 /* Backslash, whitespace, backslash is a continuation, with no decoded bytes. */
<STR>\\[ \t\r\f\n] {adjustLines(); begin(StartCondition_::IGNORE);}
<IGNORE>[ \t\r\f\n]+ {adjustLines();}
<IGNORE>\\ {adjust(); begin(StartCondition_::STR);}
<IGNORE>. {adjust(); errormsg_->Error(GetTokPos(), "expected backslash after string continuation"); begin(StartCondition_::STR);}
<STR>[^"\\\n\r\x00-\x1f]+ {adjust(); string_buf_ += matched();}
<STR>\\. {adjust(); errormsg_->Error(GetTokPos(), "invalid string escape");}
<STR>\\ {adjust(); errormsg_->Error(GetTokPos(), "incomplete string escape");}
<STR>\n {adjust(); errormsg_->Error(GetTokPos(), "unescaped newline in string"); errormsg_->Newline();}
<STR>[\r\x00-\x1f] {adjust(); errormsg_->Error(GetTokPos(), "unescaped control character in string");}
<STR,IGNORE><<EOF>> {errormsg_->Error(string_pos_, "unterminated string"); begin(StartCondition_::INITIAL); return 0;}

[ \t\r\f]+ {adjust();}
\n {adjust(); errormsg_->Newline();}
. {adjust(); errormsg_->Error(GetTokPos(), "illegal token");}

<INITIAL><<EOF>> {errormsg_->tok_pos_ = char_pos_; return 0;}
