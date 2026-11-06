%filenames parser
%scanner tiger/lex/scanner.h
%baseclass-preinclude tiger/absyn/absyn.h

 /*
  * Please don't modify the lines above.
  */

%union {
  int ival;
  std::string* sval;
  sym::Symbol *sym;
  absyn::Exp *exp;
  absyn::ExpList *explist;
  absyn::Var *var;
  absyn::DecList *declist;
  absyn::Dec *dec;
  absyn::EFieldList *efieldlist;
  absyn::EField *efield;
  absyn::NameAndTyList *tydeclist;
  absyn::NameAndTy *tydec;
  absyn::FieldList *fieldlist;
  absyn::Field *field;
  absyn::FunDecList *fundeclist;
  absyn::FunDec *fundec;
  absyn::Ty *ty;
  }

%nonassoc GROUP

%token <sym> ID
%token <sval> STRING
%token <ival> INT

%token
  COMMA COLON SEMICOLON LPAREN RPAREN LBRACK RBRACK
  LBRACE RBRACE DOT
  ARRAY IF WHILE FOR TO DO LET IN END OF
  BREAK NIL
  FUNCTION VAR TYPE

 /* Operators are structurally layered; precedence here resolves the
    boundary between a control-expression body and its enclosing expression.
    Pass-through rules use CONTROL too: bisonc++ otherwise assigns a rule with
    no terminals maximum priority, potentially ending a loop/if body early. */
%nonassoc CONTROL
%nonassoc THEN
%nonassoc ELSE
%right ASSIGN
%left OR
%left AND
%nonassoc EQ NEQ LT LE GT GE
%left PLUS MINUS
%left TIMES DIVIDE
%right UMINUS

%type <exp> exp or_exp and_exp compare_exp add_exp mul_exp unary_exp atom
%type <explist> actuals nonemptyactuals expseq sequence
%type <var> lvalue nontrivial_lvalue
%type <declist> decs
%type <dec> declaration vardec
%type <efieldlist> record_fields nonempty_record_fields
%type <efield> record_field
%type <tydeclist> type_group
%type <tydec> type_decl
%type <fieldlist> fields nonempty_fields
%type <field> field
%type <ty> ty
%type <fundeclist> function_group
%type <fundec> function_decl
%start program

%%
program: exp {absyn_tree_ = std::make_unique<absyn::AbsynTree>($1);};
exp:
    lvalue ASSIGN exp {$$ = new absyn::AssignExp($1->pos_, $1, $3);}
  | or_exp %prec CONTROL {$$ = $1;}
  ;
or_exp:
    and_exp %prec CONTROL {$$ = $1;}
  | or_exp OR and_exp {$$ = new absyn::OpExp($1->pos_, absyn::OR_OP, $1, $3);}
  ;
and_exp:
    compare_exp %prec CONTROL {$$ = $1;}
  | and_exp AND compare_exp {$$ = new absyn::OpExp($1->pos_, absyn::AND_OP, $1, $3);}
  ;
 /* A comparison has exactly two additive operands: a < b < c is invalid. */
compare_exp:
    add_exp %prec CONTROL {$$ = $1;}
  | add_exp EQ add_exp {$$ = new absyn::OpExp($1->pos_, absyn::EQ_OP, $1, $3);}
  | add_exp NEQ add_exp {$$ = new absyn::OpExp($1->pos_, absyn::NEQ_OP, $1, $3);}
  | add_exp LT add_exp {$$ = new absyn::OpExp($1->pos_, absyn::LT_OP, $1, $3);}
  | add_exp LE add_exp {$$ = new absyn::OpExp($1->pos_, absyn::LE_OP, $1, $3);}
  | add_exp GT add_exp {$$ = new absyn::OpExp($1->pos_, absyn::GT_OP, $1, $3);}
  | add_exp GE add_exp {$$ = new absyn::OpExp($1->pos_, absyn::GE_OP, $1, $3);}
  ;
add_exp:
    mul_exp %prec CONTROL {$$ = $1;}
  | add_exp PLUS mul_exp {$$ = new absyn::OpExp($1->pos_, absyn::PLUS_OP, $1, $3);}
  | add_exp MINUS mul_exp {$$ = new absyn::OpExp($1->pos_, absyn::MINUS_OP, $1, $3);}
  ;
mul_exp:
    unary_exp %prec CONTROL {$$ = $1;}
  | mul_exp TIMES unary_exp {$$ = new absyn::OpExp($1->pos_, absyn::TIMES_OP, $1, $3);}
  | mul_exp DIVIDE unary_exp {$$ = new absyn::OpExp($1->pos_, absyn::DIVIDE_OP, $1, $3);}
  ;
unary_exp:
    atom %prec CONTROL {$$ = $1;}
  | MINUS unary_exp %prec UMINUS {$$ = new absyn::OpExp($2->pos_, absyn::MINUS_OP, new absyn::IntExp($2->pos_, 0), $2);}
  ;
atom:
    lvalue %prec CONTROL {$$ = new absyn::VarExp($1->pos_, $1);}
  | NIL {$$ = new absyn::NilExp(scanner_.GetTokPos());}
  | INT {$$ = new absyn::IntExp(scanner_.GetTokPos(), $1);}
  | STRING {$$ = new absyn::StringExp(scanner_.GetTokPos(), $1);}
  | ID LPAREN actuals RPAREN {$$ = new absyn::CallExp(scanner_.GetTokPos(), $1, $3);}
  | ID LBRACE record_fields RBRACE {$$ = new absyn::RecordExp(scanner_.GetTokPos(), $1, $3);}
  | ID LBRACK exp RBRACK OF exp %prec CONTROL {$$ = new absyn::ArrayExp(scanner_.GetTokPos(), $1, $3, $6);}
  | LPAREN RPAREN {$$ = new absyn::VoidExp(scanner_.GetTokPos());}
  | LPAREN sequence RPAREN {$$ = new absyn::SeqExp(scanner_.GetTokPos(), $2);}
  | IF exp THEN exp %prec THEN {$$ = new absyn::IfExp(scanner_.GetTokPos(), $2, $4, nullptr);}
  | IF exp THEN exp ELSE exp %prec CONTROL {$$ = new absyn::IfExp(scanner_.GetTokPos(), $2, $4, $6);}
  | WHILE exp DO exp %prec CONTROL {$$ = new absyn::WhileExp(scanner_.GetTokPos(), $2, $4);}
  | FOR ID ASSIGN exp TO exp DO exp %prec CONTROL {$$ = new absyn::ForExp(scanner_.GetTokPos(), $2, $4, $6, $8);}
  | BREAK {$$ = new absyn::BreakExp(scanner_.GetTokPos());}
  | LET decs IN expseq END {$$ = new absyn::LetExp(scanner_.GetTokPos(), $2, new absyn::SeqExp(scanner_.GetTokPos(), $4));}
  ;
 /* Keep ID [exp] unreduced until OF distinguishes construction from access. */
lvalue:
    ID %prec GROUP {$$ = new absyn::SimpleVar(scanner_.GetTokPos(), $1);}
  | nontrivial_lvalue {$$ = $1;}
  ;
nontrivial_lvalue:
    ID DOT ID {$$ = new absyn::FieldVar(scanner_.GetTokPos(), new absyn::SimpleVar(scanner_.GetTokPos(), $1), $3);}
  | ID LBRACK exp RBRACK {$$ = new absyn::SubscriptVar(scanner_.GetTokPos(), new absyn::SimpleVar(scanner_.GetTokPos(), $1), $3);}
  | nontrivial_lvalue DOT ID {$$ = new absyn::FieldVar($1->pos_, $1, $3);}
  | nontrivial_lvalue LBRACK exp RBRACK {$$ = new absyn::SubscriptVar($1->pos_, $1, $3);}
  ;
actuals:
    {$$ = new absyn::ExpList();}
  | nonemptyactuals {$$ = $1;}
  ;
nonemptyactuals:
    exp {$$ = new absyn::ExpList($1);}
  | exp COMMA nonemptyactuals {$$ = $3->Prepend($1);}
  ;
expseq:
    {$$ = new absyn::ExpList();}
  | sequence {$$ = $1;}
  ;
sequence:
    exp {$$ = new absyn::ExpList($1);}
  | exp SEMICOLON sequence {$$ = $3->Prepend($1);}
  ;
record_fields:
    {$$ = new absyn::EFieldList();}
  | nonempty_record_fields {$$ = $1;}
  ;
nonempty_record_fields:
    record_field {$$ = new absyn::EFieldList($1);}
  | record_field COMMA nonempty_record_fields {$$ = $3->Prepend($1);}
  ;
record_field: ID EQ exp {$$ = new absyn::EField($1, $3);};
decs:
    {$$ = new absyn::DecList();}
  | declaration decs {$$ = $2->Prepend($1);}
  ;
declaration:
    vardec {$$ = $1;}
  | type_group {$$ = new absyn::TypeDec(scanner_.GetTokPos(), $1);}
  | function_group {$$ = new absyn::FunctionDec(scanner_.GetTokPos(), $1);}
  ;
vardec:
    VAR ID ASSIGN exp {$$ = new absyn::VarDec(scanner_.GetTokPos(), $2, nullptr, $4);}
  | VAR ID COLON ID ASSIGN exp {$$ = new absyn::VarDec(scanner_.GetTokPos(), $2, $4, $6);}
  ;
type_group:
    type_decl %prec GROUP {$$ = new absyn::NameAndTyList($1);}
  | type_decl type_group {$$ = $2->Prepend($1);}
  ;
type_decl: TYPE ID EQ ty {$$ = new absyn::NameAndTy($2, $4);};
ty:
    ID {$$ = new absyn::NameTy(scanner_.GetTokPos(), $1);}
  | LBRACE fields RBRACE {$$ = new absyn::RecordTy(scanner_.GetTokPos(), $2);}
  | ARRAY OF ID {$$ = new absyn::ArrayTy(scanner_.GetTokPos(), $3);}
  ;
fields:
    {$$ = new absyn::FieldList();}
  | nonempty_fields {$$ = $1;}
  ;
nonempty_fields:
    field {$$ = new absyn::FieldList($1);}
  | field COMMA nonempty_fields {$$ = $3->Prepend($1);}
  ;
field: ID COLON ID {$$ = new absyn::Field(scanner_.GetTokPos(), $1, $3);};
function_group:
    function_decl %prec GROUP {$$ = new absyn::FunDecList($1);}
  | function_decl function_group {$$ = $2->Prepend($1);}
  ;
function_decl:
    FUNCTION ID LPAREN fields RPAREN EQ exp {$$ = new absyn::FunDec(scanner_.GetTokPos(), $2, $4, nullptr, $7);}
  | FUNCTION ID LPAREN fields RPAREN COLON ID EQ exp {$$ = new absyn::FunDec(scanner_.GetTokPos(), $2, $4, $7, $9);}
  ;
