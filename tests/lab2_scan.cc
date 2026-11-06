#include "tiger/lex/scanner.h"
#include <cstdio>
#include <stdexcept>

// Preserve all bytes, including NUL, which printf("%s") in the official
// human-readable driver cannot expose.
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  Scanner scanner(argv[1]);
  while (int token = scanner.lex()) {
    const char *name = token == Parser::ID ? "ID" :
                       token == Parser::STRING ? "STRING" :
                       token == Parser::INT ? "INT" : "OTHER";
    std::printf("%s %d ", name, scanner.GetTokPos());
    for (unsigned char c : scanner.matched()) std::printf("%02x", c);
    std::printf("\n");
  }
  // EOF must remain stable, including after an unterminated lexical state.
  if (scanner.lex() != 0) throw std::runtime_error("unstable EOF");
  return scanner.TransferErrormsg()->AnyErrors() ? 1 : 0;
}
