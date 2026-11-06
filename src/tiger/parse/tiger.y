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

%ltype int

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
program: exp {@@ = @1; absyn_tree_ = std::make_unique<absyn::AbsynTree>($1);};
exp:
    lvalue ASSIGN exp {@@ = @1; $$ = new absyn::AssignExp($1->pos_, $1, $3);}
  | or_exp {@@ = @1; $$ = $1;}
  ;
or_exp:
    and_exp {@@ = @1; $$ = $1;}
  | or_exp OR and_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::OR_OP, $1, $3);}
  ;
and_exp:
    compare_exp {@@ = @1; $$ = $1;}
  | and_exp AND compare_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::AND_OP, $1, $3);}
  ;
 /* A comparison has exactly two additive operands: a < b < c is invalid. */
compare_exp:
    add_exp {@@ = @1; $$ = $1;}
  | add_exp EQ add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::EQ_OP, $1, $3);}
  | add_exp NEQ add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::NEQ_OP, $1, $3);}
  | add_exp LT add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::LT_OP, $1, $3);}
  | add_exp LE add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::LE_OP, $1, $3);}
  | add_exp GT add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::GT_OP, $1, $3);}
  | add_exp GE add_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::GE_OP, $1, $3);}
  ;
add_exp:
    mul_exp {@@ = @1; $$ = $1;}
  | add_exp PLUS mul_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::PLUS_OP, $1, $3);}
  | add_exp MINUS mul_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::MINUS_OP, $1, $3);}
  ;
mul_exp:
    unary_exp {@@ = @1; $$ = $1;}
  | mul_exp TIMES unary_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::TIMES_OP, $1, $3);}
  | mul_exp DIVIDE unary_exp {@@ = @1; $$ = new absyn::OpExp($1->pos_, absyn::DIVIDE_OP, $1, $3);}
  ;
unary_exp:
    atom {@@ = @1; $$ = $1;}
  | MINUS unary_exp %prec UMINUS {@@ = @1; $$ = new absyn::OpExp(@1, absyn::MINUS_OP, new absyn::IntExp(@1, 0), $2);}
  ;
atom:
    lvalue {@@ = @1; $$ = new absyn::VarExp($1->pos_, $1);}
  | NIL {@@ = @1; $$ = new absyn::NilExp(@1);}
  | INT {@@ = @1; $$ = new absyn::IntExp(@1, $1);}
  | STRING {@@ = @1; $$ = new absyn::StringExp(@1, $1);}
  | ID LPAREN actuals RPAREN {@@ = @1; $$ = new absyn::CallExp(@1, $1, $3);}
  | ID LBRACE record_fields RBRACE {@@ = @1; $$ = new absyn::RecordExp(@1, $1, $3);}
  | ID LBRACK exp RBRACK OF exp %prec CONTROL {@@ = @1; $$ = new absyn::ArrayExp(@1, $1, $3, $6);}
  | LPAREN RPAREN {@@ = @1; $$ = new absyn::VoidExp(@1);}
  | LPAREN sequence RPAREN {@@ = @1; $$ = new absyn::SeqExp(@1, $2);}
  | IF exp THEN exp %prec THEN {@@ = @1; $$ = new absyn::IfExp(@1, $2, $4, nullptr);}
  | IF exp THEN exp ELSE exp %prec CONTROL {@@ = @1; $$ = new absyn::IfExp(@1, $2, $4, $6);}
  | WHILE exp DO exp %prec CONTROL {@@ = @1; $$ = new absyn::WhileExp(@1, $2, $4);}
  | FOR ID ASSIGN exp TO exp DO exp %prec CONTROL {@@ = @1; $$ = new absyn::ForExp(@1, $2, $4, $6, $8);}
  | BREAK {@@ = @1; $$ = new absyn::BreakExp(@1);}
  | LET decs IN expseq END {@@ = @1; $$ = new absyn::LetExp(@1, $2, new absyn::SeqExp(@4, $4));}
  ;
 /* Keep ID [exp] unreduced until OF distinguishes construction from access. */
lvalue:
    ID {@@ = @1; $$ = new absyn::SimpleVar(@1, $1);}
  | nontrivial_lvalue {@@ = @1; $$ = $1;}
  ;
nontrivial_lvalue:
    ID DOT ID {@@ = @1; $$ = new absyn::FieldVar(@1, new absyn::SimpleVar(@1, $1), $3);}
  | ID LBRACK exp RBRACK {@@ = @1; $$ = new absyn::SubscriptVar(@1, new absyn::SimpleVar(@1, $1), $3);}
  | nontrivial_lvalue DOT ID {@@ = @1; $$ = new absyn::FieldVar($1->pos_, $1, $3);}
  | nontrivial_lvalue LBRACK exp RBRACK {@@ = @1; $$ = new absyn::SubscriptVar($1->pos_, $1, $3);}
  ;
actuals:
    {@@ = scanner_.GetTokPos(); $$ = new absyn::ExpList();}
  | nonemptyactuals {@@ = @1; $$ = $1;}
  ;
nonemptyactuals:
    exp {@@ = @1; $$ = new absyn::ExpList($1);}
  | exp COMMA nonemptyactuals {@@ = @1; $$ = $3->Prepend($1);}
  ;
expseq:
    {@@ = scanner_.GetTokPos(); $$ = new absyn::ExpList();}
  | sequence {@@ = @1; $$ = $1;}
  ;
sequence:
    exp {@@ = @1; $$ = new absyn::ExpList($1);}
  | exp SEMICOLON sequence {@@ = @1; $$ = $3->Prepend($1);}
  ;
record_fields:
    {@@ = scanner_.GetTokPos(); $$ = new absyn::EFieldList();}
  | nonempty_record_fields {@@ = @1; $$ = $1;}
  ;
nonempty_record_fields:
    record_field {@@ = @1; $$ = new absyn::EFieldList($1);}
  | record_field COMMA nonempty_record_fields {@@ = @1; $$ = $3->Prepend($1);}
  ;
record_field: ID EQ exp {@@ = @1; $$ = new absyn::EField($1, $3);};
decs:
    {@@ = scanner_.GetTokPos(); $$ = new absyn::DecList();}
  | declaration decs {@@ = @1; $$ = $2->Prepend($1);}
  ;
declaration:
    vardec {@@ = @1; $$ = $1;}
  | type_group {@@ = @1; $$ = new absyn::TypeDec(@1, $1);}
  | function_group {@@ = @1; $$ = new absyn::FunctionDec(@1, $1);}
  ;
vardec:
    VAR ID ASSIGN exp {@@ = @1; $$ = new absyn::VarDec(@1, $2, nullptr, $4);}
  | VAR ID COLON ID ASSIGN exp {@@ = @1; $$ = new absyn::VarDec(@1, $2, $4, $6);}
  ;
type_group:
    type_decl {@@ = @1; $$ = new absyn::NameAndTyList($1);}
  | type_decl type_group {@@ = @1; $$ = $2->Prepend($1);}
  ;
type_decl: TYPE ID EQ ty {@@ = @1; $$ = new absyn::NameAndTy($2, $4);};
ty:
    ID {@@ = @1; $$ = new absyn::NameTy(@1, $1);}
  | LBRACE fields RBRACE {@@ = @1; $$ = new absyn::RecordTy(@1, $2);}
  | ARRAY OF ID {@@ = @1; $$ = new absyn::ArrayTy(@1, $3);}
  ;
fields:
    {@@ = scanner_.GetTokPos(); $$ = new absyn::FieldList();}
  | nonempty_fields {@@ = @1; $$ = $1;}
  ;
nonempty_fields:
    field {@@ = @1; $$ = new absyn::FieldList($1);}
  | field COMMA nonempty_fields {@@ = @1; $$ = $3->Prepend($1);}
  ;
field: ID COLON ID {@@ = @1; $$ = new absyn::Field(@1, $1, $3);};
function_group:
    function_decl {@@ = @1; $$ = new absyn::FunDecList($1);}
  | function_decl function_group {@@ = @1; $$ = $2->Prepend($1);}
  ;
function_decl:
    FUNCTION ID LPAREN fields RPAREN EQ exp {@@ = @1; $$ = new absyn::FunDec(@1, $2, $4, nullptr, $7);}
  | FUNCTION ID LPAREN fields RPAREN COLON ID EQ exp {@@ = @1; $$ = new absyn::FunDec(@1, $2, $4, $7, $9);}
  ;
